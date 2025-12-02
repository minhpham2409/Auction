/*
 * =====================================================
 * CLIENT.C - ONLINE AUCTION SYSTEM CLIENT (WITH ROOM MANAGEMENT)
 * =====================================================
 * Compile: gcc client.c -o client -lpthread
 * Run: ./client
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8888
#define BUFFER_SIZE 4096

// =====================================================
// GLOBAL VARIABLES
// =====================================================

int client_socket;
int logged_in = 0;
int current_user_id = 0;
char current_username[50];
double current_balance = 0.0;
int current_room_id = 0;
char current_room_name[100] = "None";
int running = 1;

pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

// =====================================================
// UTILITY FUNCTIONS
// =====================================================

void clear_screen() {
    printf("\033[2J\033[1;1H");
}

void print_header(const char *title) {
    printf("\n");
    printf("===========================================\n");
    printf("   %s\n", title);
    printf("===========================================\n");
}

void print_separator() {
    printf("-------------------------------------------\n");
}

void safe_print(const char *format, ...) {
    pthread_mutex_lock(&print_mutex);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
    pthread_mutex_unlock(&print_mutex);
}

// =====================================================
// NETWORK FUNCTIONS
// =====================================================

int send_request(const char *request) {
    return send(client_socket, request, strlen(request), 0);
}

int receive_response(char *buffer, int size) {
    memset(buffer, 0, size);

    // Set timeout for recv
    struct timeval tv;
    tv.tv_sec = 10;  // 10 second timeout
    tv.tv_usec = 0;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int bytes = recv(client_socket, buffer, size - 1, 0);

    if (bytes > 0) {
        buffer[bytes] = '\0';
    } else if (bytes == 0) {
        // Connection closed
        printf("\n[ERROR] Server disconnected!\n");
        running = 0;
    } else {
        // Error or timeout
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("\n[ERROR] Request timeout! Server may be busy.\n");
        } else {
            printf("\n[ERROR] Connection error!\n");
            running = 0;
        }
        bytes = -1;
    }

    return bytes;
}

// =====================================================
// NOTIFICATION LISTENER THREAD
// =====================================================

void* notification_listener(void *arg) {
    char buffer[BUFFER_SIZE];
    fd_set read_fds;
    struct timeval tv;

    while (running) {
        FD_ZERO(&read_fds);
        FD_SET(client_socket, &read_fds);

        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int activity = select(client_socket + 1, &read_fds, NULL, NULL, &tv);

        if (activity < 0) {
            continue;
        }

        if (activity == 0) {
            continue;
        }

        if (FD_ISSET(client_socket, &read_fds)) {
            int bytes = recv(client_socket, buffer, BUFFER_SIZE - 1, MSG_DONTWAIT);

            if (bytes <= 0) {
                if (bytes == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                    running = 0;
                    safe_print("\n[ERROR] Connection lost to server!\n");
                    break;
                }
                continue;
            }

            buffer[bytes] = '\0';

            // Ignore response messages
            if (strstr(buffer, "_SUCCESS") != NULL ||
                strstr(buffer, "_FAIL") != NULL ||
                strstr(buffer, "AUCTION_LIST") != NULL ||
                strstr(buffer, "AUCTION_DETAIL") != NULL ||
                strstr(buffer, "BID_HISTORY") != NULL ||
                strstr(buffer, "MY_AUCTIONS") != NULL ||
                strstr(buffer, "ROOM_LIST") != NULL ||
                strstr(buffer, "ROOM_DETAIL") != NULL ||
                strstr(buffer, "MY_ROOM") != NULL) {
                continue;
            }

            // Handle force logout
            if (strncmp(buffer, "FORCE_LOGOUT|", 13) == 0) {
                char reason[256];
                sscanf(buffer + 13, "%[^\n]", reason);
                safe_print("\n╔════════════════════════════════════════════════════════╗\n");
                safe_print("║          ⚠️  FORCE LOGOUT ⚠️                          ║\n");
                safe_print("╠════════════════════════════════════════════════════════╣\n");
                safe_print("║ Reason: %s\n", reason);
                safe_print("║ You have been logged out.                              ║\n");
                safe_print("╚════════════════════════════════════════════════════════╝\n");
                running = 0;
                break;
            }

            // Handle room notifications
            if (strncmp(buffer, "NEW_ROOM|", 9) == 0) {
                int room_id, max_participants;
                char room_name[100], creator[50];
                sscanf(buffer + 9, "%d|%[^|]|%[^|]|%d",
                       &room_id, room_name, creator, &max_participants);

                safe_print("\n╔════════════════════════════════════════════════════════╗\n");
                safe_print("║          🏠 NEW ROOM CREATED! 🏠                      ║\n");
                safe_print("╠════════════════════════════════════════════════════════╣\n");
                safe_print("║ Room ID:         %-5d                                ║\n", room_id);
                safe_print("║ Room Name:       %-40s ║\n", room_name);
                safe_print("║ Created By:      %-40s ║\n", creator);
                safe_print("║ Max Participants: %-3d                                 ║\n", max_participants);
                safe_print("╚════════════════════════════════════════════════════════╝\n");
                safe_print(">> ");
            }
            else if (strncmp(buffer, "USER_JOINED|", 12) == 0) {
                char username[50];
                int room_id;
                sscanf(buffer + 12, "%[^|]|%d", username, &room_id);

                safe_print("\n╔════════════════════════════════════════════════════════╗\n");
                safe_print("║          👤 USER JOINED ROOM 👤                       ║\n");
                safe_print("╠════════════════════════════════════════════════════════╣\n");
                safe_print("║ User: %-48s ║\n", username);
                safe_print("║ Room ID: %-5d                                        ║\n", room_id);
                safe_print("╚════════════════════════════════════════════════════════╝\n");
                safe_print(">> ");
            }
            else if (strncmp(buffer, "USER_LEFT|", 10) == 0) {
                char username[50];
                int room_id;
                sscanf(buffer + 10, "%[^|]|%d", username, &room_id);

                safe_print("\n╔════════════════════════════════════════════════════════╗\n");
                safe_print("║          👋 USER LEFT ROOM 👋                         ║\n");
                safe_print("╠════════════════════════════════════════════════════════╣\n");
                safe_print("║ User: %-48s ║\n", username);
                safe_print("║ Room ID: %-5d                                        ║\n", room_id);
                safe_print("╚════════════════════════════════════════════════════════╝\n");
                safe_print(">> ");
            }
            // Handle auction notifications
            else if (strncmp(buffer, "NEW_AUCTION|", 12) == 0) {
                int auction_id, time_left;
                char title[200];
                double start_price, buy_now_price, min_increment;

                if (sscanf(buffer + 12, "%d|%[^|]|%lf|%lf|%lf|%d",
                          &auction_id, title, &start_price, &buy_now_price,
                          &min_increment, &time_left) == 6) {

                    safe_print("\n╔════════════════════════════════════════════════════════╗\n");
                    safe_print("║          🔔 NEW AUCTION CREATED! 🔔                   ║\n");
                    safe_print("╠════════════════════════════════════════════════════════╣\n");
                    safe_print("║ ID:              %-5d                                ║\n", auction_id);
                    safe_print("║ Title:           %-40s ║\n", title);
                    safe_print("║ Starting Price:  %12.2f VND                   ║\n", start_price);
                    safe_print("║ Buy Now Price:   %12.2f VND                   ║\n", buy_now_price);
                    safe_print("║ Min Increment:   %12.2f VND                   ║\n", min_increment);
                    safe_print("║ Duration:        %3d hours %2d minutes               ║\n",
                              time_left / 3600, (time_left % 3600) / 60);
                    safe_print("╚════════════════════════════════════════════════════════╝\n");
                    safe_print(">> ");
                }

            } else if (strncmp(buffer, "NEW_BID_WARNING|", 16) == 0) {
                int auction_id, total_bids, time_left;
                char bidder[50];
                double bid_amount;
                sscanf(buffer + 16, "%d|%[^|]|%lf|%d|%d",
                       &auction_id, bidder, &bid_amount, &total_bids, &time_left);

                safe_print("\n╔════════════════════════════════════════════════════════╗\n");
                safe_print("║      ⚠️  WARNING: LAST 30 SECONDS! ⚠️                ║\n");
                safe_print("╠════════════════════════════════════════════════════════╣\n");
                safe_print("║          💰 NEW BID PLACED! 💰                        ║\n");
                safe_print("╠════════════════════════════════════════════════════════╣\n");
                safe_print("║ Auction ID:      #%-5d                              ║\n", auction_id);
                safe_print("║ Bidder:          %-40s ║\n", bidder);
                safe_print("║ Bid Amount:      %12.2f VND                   ║\n", bid_amount);
                safe_print("║ Total Bids:      %-5d                                ║\n", total_bids);
                safe_print("║ ⏰ Time Left:     %2d seconds (EXTENDED!)             ║\n", time_left);
                safe_print("╚════════════════════════════════════════════════════════╝\n");
                safe_print(">> ");

            } else if (strncmp(buffer, "NEW_BID|", 8) == 0) {
                int auction_id, total_bids;
                char bidder[50];
                double bid_amount;
                sscanf(buffer + 8, "%d|%[^|]|%lf|%d",
                       &auction_id, bidder, &bid_amount, &total_bids);

                safe_print("\n╔════════════════════════════════════════════════════════╗\n");
                safe_print("║          💰 NEW BID PLACED! 💰                        ║\n");
                safe_print("╠════════════════════════════════════════════════════════╣\n");
                safe_print("║ Auction ID:      #%-5d                              ║\n", auction_id);
                safe_print("║ Bidder:          %-40s ║\n", bidder);
                safe_print("║ Bid Amount:      %12.2f VND                   ║\n", bid_amount);
                safe_print("║ Total Bids:      %-5d                                ║\n", total_bids);
                safe_print("╚════════════════════════════════════════════════════════╝\n");
                safe_print(">> ");

            } else if (strncmp(buffer, "AUCTION_WARNING|", 16) == 0) {
                int auction_id, time_left;
                char title[200];
                double current_price;
                sscanf(buffer + 16, "%d|%[^|]|%lf|%d",
                       &auction_id, title, &current_price, &time_left);

                safe_print("\n");
                safe_print("╔════════════════════════════════════════════════════════╗\n");
                safe_print("║                                                        ║\n");
                safe_print("║      ⏰⏰⏰ URGENT: 30 SECONDS LEFT! ⏰⏰⏰          ║\n");
                safe_print("║                                                        ║\n");
                safe_print("╠════════════════════════════════════════════════════════╣\n");
                safe_print("║ Auction ID:      #%-5d                              ║\n", auction_id);
                safe_print("║ Title:           %-38s   ║\n", title);
                safe_print("║ Current Price:   %12.2f VND                   ║\n", current_price);
                safe_print("║ Time Left:       %-2d SECONDS! HURRY!                 ║\n", time_left);
                safe_print("║                                                        ║\n");
                safe_print("║          🔥 LAST CHANCE TO BID! 🔥                    ║\n");
                safe_print("║                                                        ║\n");
                safe_print("╚════════════════════════════════════════════════════════╝\n");
                safe_print(">> ");

            } else if (strncmp(buffer, "AUCTION_ENDED|", 14) == 0) {
                int auction_id, total_bids;
                char title[200], winner_name[50];
                double final_price;

                // Parse: AUCTION_ENDED|auction_id|title|winner|price|total_bids
                if (sscanf(buffer + 14, "%d|%[^|]|%[^|]|%lf|%d",
                           &auction_id, title, winner_name, &final_price, &total_bids) == 5) {

                    safe_print("\n");
                    safe_print("╔════════════════════════════════════════════════════════╗\n");
                    safe_print("║                                                        ║\n");
                    safe_print("║          🏆🏆🏆 AUCTION ENDED! 🏆🏆🏆              ║\n");
                    safe_print("║                                                        ║\n");
                    safe_print("╠════════════════════════════════════════════════════════╣\n");
                    safe_print("║ Auction ID:      #%-5d                              ║\n", auction_id);
                    safe_print("║ Title:           %-38s   ║\n", title);
                    safe_print("╠════════════════════════════════════════════════════════╣\n");

                    if (strcmp(winner_name, "No bids") == 0) {
                        safe_print("║                                                        ║\n");
                        safe_print("║          ❌ NO WINNER - NO BIDS PLACED ❌            ║\n");
                        safe_print("║                                                        ║\n");
                        safe_print("║ Starting Price:  %12.2f VND                   ║\n", final_price);
                    } else {
                        // Check if current user is the winner
                        if (strcmp(winner_name, current_username) == 0) {
                            safe_print("║                                                        ║\n");
                            safe_print("║     🎉🎉🎉 CONGRATULATIONS! YOU WON! 🎉🎉🎉       ║\n");
                            safe_print("║                                                        ║\n");
                        } else {
                            safe_print("║                                                        ║\n");
                            safe_print("║               🏆 WINNER ANNOUNCED! 🏆                 ║\n");
                            safe_print("║                                                        ║\n");
                        }
                        safe_print("║ Winner:          %-38s   ║\n", winner_name);
                        safe_print("║ Final Price:     %12.2f VND                   ║\n", final_price);
                        safe_print("║ Total Bids:      %-5d                                ║\n", total_bids);
                    }

                    safe_print("╚════════════════════════════════════════════════════════╝\n");
                } else {
                    // Fallback for old format
                    safe_print("\n╔════════════════════════════════════════════════════════╗\n");
                    safe_print("║          🏁 AUCTION ENDED! 🏁                         ║\n");
                    safe_print("╚════════════════════════════════════════════════════════╝\n");
                }
                safe_print(">> ");
            }
        }
    }

    return NULL;
}

// =====================================================
// MENU FUNCTIONS
// =====================================================

void display_main_menu() {
    print_header("ONLINE AUCTION SYSTEM");
    printf("1. Register\n");
    printf("2. Login\n");
    printf("3. Exit\n");
    print_separator();
    printf("Choose option: ");
}

void display_user_menu() {
    print_header("MAIN MENU");
    printf("User: %s | Balance: %.2f VND\n", current_username, current_balance);
    printf("Current Room: %s (ID: %d)\n", current_room_name, current_room_id);
    print_separator();
    printf("ROOM MANAGEMENT:\n");
    printf("1. Create New Room\n");
    printf("2. List All Rooms\n");
    printf("3. Join Room\n");
    printf("4. Leave Room\n");
    printf("5. View Room Detail\n");
    printf("6. View My Current Room\n");
    print_separator();
    printf("AUCTION MANAGEMENT:\n");
    printf("7. List Auctions (in current room)\n");
    printf("8. View My Auctions\n");
    printf("9. View Auction Detail\n");
    printf("10. Create New Auction\n");
    printf("11. Place Bid\n");
    printf("12. Buy Now\n");
    printf("13. View Bid History\n");
    printf("14. View Auction History (Completed)\n");
    print_separator();
    printf("15. Logout\n");
    print_separator();
    printf("Choose option: ");
}

// =====================================================
// ROOM FEATURE FUNCTIONS
// =====================================================

void create_room() {
    char name[100], desc[200];
    int max_participants, duration;

    print_header("CREATE NEW ROOM");

    getchar(); // Clear buffer

    printf("Room Name: ");
    fgets(name, sizeof(name), stdin);
    name[strcspn(name, "\n")] = 0;

    printf("Description: ");
    fgets(desc, sizeof(desc), stdin);
    desc[strcspn(desc, "\n")] = 0;

    printf("Max Participants: ");
    scanf("%d", &max_participants);

    printf("Duration (minutes): ");
    scanf("%d", &duration);

    char request[BUFFER_SIZE];
    sprintf(request, "CREATE_ROOM|%d|%s|%s|%d|%d\n",
            current_user_id, name, desc, max_participants, duration);

    send_request(request);

    char response[BUFFER_SIZE];
    int bytes = receive_response(response, BUFFER_SIZE);

    if (bytes <= 0) {
        printf("\n[ERROR] Connection lost!\n");
        printf("\nPress Enter to continue...");
        getchar();
        getchar();
        return;
    }

    if (strncmp(response, "CREATE_ROOM_SUCCESS", 19) == 0) {
        int room_id;
        char room_name[100];
        sscanf(response, "CREATE_ROOM_SUCCESS|%d|%[^\n]", &room_id, room_name);

        // Auto update current room (creator is auto-joined)
        current_room_id = room_id;
        strncpy(current_room_name, room_name, 99);
        current_room_name[99] = '\0';

        printf("\n╔════════════════════════════════════════════════════════╗\n");
        printf("║          ✅ ROOM CREATED SUCCESSFULLY! ✅             ║\n");
        printf("╠════════════════════════════════════════════════════════╣\n");
        printf("║ Room ID:         %-5d                                ║\n", room_id);
        printf("║ Room Name:       %-40s ║\n", room_name);
        printf("║ Status:          You are now in this room!            ║\n");
        printf("╚════════════════════════════════════════════════════════╝\n");
    } else if (strncmp(response, "CREATE_ROOM_FAIL", 16) == 0) {
        char error[256];
        sscanf(response, "CREATE_ROOM_FAIL|%[^\n]", error);
        printf("\n[ERROR] Failed to create room: %s\n", error);
    } else {
        printf("\n[ERROR] Invalid response from server!\n");
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

void list_rooms() {
    print_header("AVAILABLE ROOMS");

    send_request("LIST_ROOMS|\n");

    char response[BUFFER_SIZE * 4];
    receive_response(response, BUFFER_SIZE * 4);

    if (strncmp(response, "ROOM_LIST|", 10) == 0) {
        char *data = response + 10;
        char *token = strtok(data, "|");

        int count = 0;
        printf("\n%-5s %-25s %-12s %-12s %-10s %-10s %s\n",
               "ID", "Name", "Participants", "Max", "Status", "Time Left", "Auctions");
        print_separator();

        while (token != NULL) {
            int id, current_part, max_part, time_left, total_auctions;
            char name[100], desc[200], status[20];

            if (sscanf(token, "%d;%[^;];%[^;];%d;%d;%19[^;];%d;%d",
                      &id, name, desc, &current_part, &max_part,
                      status, &time_left, &total_auctions) == 8) {

                int hours = time_left / 3600;
                int minutes = (time_left % 3600) / 60;

                printf("%-5d %-25s %5d / %-5d %-10s %3dh %2dm %8d\n",
                       id, name, current_part, max_part, status,
                       hours, minutes, total_auctions);
                count++;
            }

            token = strtok(NULL, "|");
        }

        if (count == 0) {
            printf("No rooms available.\n");
        } else {
            printf("\nTotal: %d room(s)\n", count);
        }
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

void join_room() {
    int room_id;

    print_header("JOIN ROOM");

    printf("Enter Room ID to join: ");
    if (scanf("%d", &room_id) != 1) {
        printf("\n[ERROR] Invalid input!\n");
        while (getchar() != '\n');
        printf("\nPress Enter to continue...");
        getchar();
        return;
    }

    char request[256];
    sprintf(request, "JOIN_ROOM|%d|%d\n", current_user_id, room_id);

    printf("[DEBUG] Sending: %s", request);

    if (send_request(request) <= 0) {
        printf("[ERROR] Failed to send request!\n");
        printf("\nPress Enter to continue...");
        getchar();
        getchar();
        return;
    }

    printf("[DEBUG] Waiting for response...\n");

    char response[BUFFER_SIZE];
    int bytes = receive_response(response, BUFFER_SIZE);

    if (bytes <= 0) {
        printf("\n[ERROR] Connection lost or timeout!\n");
        printf("\nPress Enter to continue...");
        getchar();
        getchar();
        return;
    }

    printf("[DEBUG] Received: %s\n", response);

    if (strncmp(response, "JOIN_ROOM_SUCCESS", 17) == 0) {
        char room_name[100];
        if (sscanf(response, "JOIN_ROOM_SUCCESS|%d|%[^\n]", &current_room_id, room_name) == 2) {
            strncpy(current_room_name, room_name, 99);
            current_room_name[99] = '\0';

            printf("\n╔════════════════════════════════════════════════════════╗\n");
            printf("║          ✅ JOINED ROOM SUCCESSFULLY! ✅              ║\n");
            printf("╠════════════════════════════════════════════════════════╣\n");
            printf("║ Room ID:         %-5d                                ║\n", current_room_id);
            printf("║ Room Name:       %-40s ║\n", current_room_name);
            printf("╚════════════════════════════════════════════════════════╝\n");
        } else {
            printf("\n[ERROR] Failed to parse response!\n");
        }
    } else if (strncmp(response, "JOIN_ROOM_FAIL", 14) == 0) {
        char error[256];
        if (sscanf(response, "JOIN_ROOM_FAIL|%[^\n]", error) == 1) {
            printf("\n[ERROR] Failed to join room:\n%s\n", error);
        } else {
            printf("\n[ERROR] Failed to join room!\n");
        }
    } else {
        printf("\n[ERROR] Invalid response from server!\n");
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

void leave_room() {
    print_header("LEAVE ROOM");

    if (current_room_id == 0) {
        printf("You are not in any room.\n");
        printf("\nPress Enter to continue...");
        getchar();
        getchar();
        return;
    }

    printf("Current Room: %s (ID: %d)\n", current_room_name, current_room_id);
    printf("Are you sure you want to leave? (y/n): ");

    char confirm;
    scanf(" %c", &confirm);

    if (confirm != 'y' && confirm != 'Y') {
        printf("Cancelled.\n");
        printf("\nPress Enter to continue...");
        getchar();
        getchar();
        return;
    }

    char request[256];
    sprintf(request, "LEAVE_ROOM|%d\n", current_user_id);
    send_request(request);

    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);

    if (strncmp(response, "LEAVE_ROOM_SUCCESS", 18) == 0) {
        printf("\n[SUCCESS] Left room successfully!\n");
        current_room_id = 0;
        strcpy(current_room_name, "None");
    } else {
        char error[256];
        sscanf(response, "LEAVE_ROOM_FAIL|%[^\n]", error);
        printf("\n[ERROR] Failed to leave room: %s\n", error);
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

void view_room_detail() {
    int room_id;

    print_header("ROOM DETAIL");

    printf("Enter Room ID: ");
    if (scanf("%d", &room_id) != 1) {
        printf("\n[ERROR] Invalid input!\n");
        // Clear input buffer
        while (getchar() != '\n');
        printf("\nPress Enter to continue...");
        getchar();
        return;
    }

    char request[256];
    sprintf(request, "ROOM_DETAIL|%d\n", room_id);
    send_request(request);

    char response[BUFFER_SIZE];
    int bytes = receive_response(response, BUFFER_SIZE);

    if (bytes <= 0) {
        printf("\n[ERROR] Connection lost!\n");
        printf("\nPress Enter to continue...");
        getchar();
        getchar();
        return;
    }

    if (strncmp(response, "ROOM_DETAIL|", 12) == 0) {
        int id, current_part, max_part, time_left, total_auctions;
        char name[100], desc[200], creator[50], status[20];

        if (sscanf(response + 12, "%d|%[^|]|%[^|]|%[^|]|%d|%d|%19[^|]|%d|%d",
               &id, name, desc, creator, &current_part, &max_part,
               status, &time_left, &total_auctions) == 9) {

            printf("\n");
            printf("Room ID          : %d\n", id);
            printf("Room Name        : %s\n", name);
            printf("Description      : %s\n", desc);
            printf("Created By       : %s\n", creator);
            printf("Participants     : %d / %d\n", current_part, max_part);
            printf("Status           : %s\n", status);
            printf("Time Remaining   : %d hours %d minutes\n",
                   time_left / 3600, (time_left % 3600) / 60);
            printf("Total Auctions   : %d\n", total_auctions);
        } else {
            printf("\n[ERROR] Failed to parse room data!\n");
        }
    } else if (strncmp(response, "ROOM_DETAIL_FAIL", 16) == 0) {
        char error[256];
        if (sscanf(response, "ROOM_DETAIL_FAIL|%[^\n]", error) == 1) {
            printf("\n[ERROR] %s\n", error);
        } else {
            printf("\n[ERROR] Room not found!\n");
        }
    } else {
        printf("\n[ERROR] Invalid response from server!\n");
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

void view_my_room() {
    print_header("MY CURRENT ROOM");

    char request[256];
    sprintf(request, "MY_ROOM|%d\n", current_user_id);
    send_request(request);

    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);

    if (strncmp(response, "MY_ROOM|", 8) == 0) {
        int room_id, participants, auctions;
        char room_name[100];

        sscanf(response + 8, "%d|%[^|]|%d|%d",
               &room_id, room_name, &participants, &auctions);

        if (room_id == 0) {
            printf("\nYou are not in any room.\n");
        } else {
            printf("\n");
            printf("Room ID          : %d\n", room_id);
            printf("Room Name        : %s\n", room_name);
            printf("Participants     : %d\n", participants);
            printf("Total Auctions   : %d\n", auctions);

            // Update local variables
            current_room_id = room_id;
            strcpy(current_room_name, room_name);
        }
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

// =====================================================
// USER FEATURE FUNCTIONS
// =====================================================

void register_user() {
    char username[50], password[256], email[100];

    print_header("USER REGISTRATION");

    printf("Username: ");
    scanf("%s", username);

    printf("Password: ");
    scanf("%s", password);

    printf("Email: ");
    scanf("%s", email);

    char request[BUFFER_SIZE];
    sprintf(request, "REGISTER|%s %s %s\n", username, password, email);

    send_request(request);

    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);

    if (strncmp(response, "REGISTER_SUCCESS", 16) == 0) {
        int user_id;
        char user[50];
        sscanf(response, "REGISTER_SUCCESS|%d|%s", &user_id, user);
        printf("\n[SUCCESS] Registration successful!\n");
        printf("User ID: %d | Username: %s\n", user_id, user);
    } else {
        char error[256];
        sscanf(response, "REGISTER_FAIL|%[^\n]", error);
        printf("\n[ERROR] Registration failed: %s\n", error);
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

void login_user() {
    char username[50], password[256];

    print_header("USER LOGIN");

    printf("Username: ");
    if (scanf("%49s", username) != 1) {
        printf("\n[ERROR] Invalid input!\n");
        while (getchar() != '\n');
        printf("\nPress Enter to continue...");
        getchar();
        return;
    }

    printf("Password: ");
    if (scanf("%255s", password) != 1) {
        printf("\n[ERROR] Invalid input!\n");
        while (getchar() != '\n');
        printf("\nPress Enter to continue...");
        getchar();
        return;
    }

    char request[BUFFER_SIZE];
    sprintf(request, "LOGIN|%s %s\n", username, password);

    printf("\n[INFO] Sending login request...\n");

    if (send_request(request) <= 0) {
        printf("[ERROR] Failed to send request!\n");
        printf("\nPress Enter to continue...");
        getchar();
        getchar();
        return;
    }

    printf("[INFO] Waiting for response...\n");

    char response[BUFFER_SIZE];
    int bytes = receive_response(response, BUFFER_SIZE);

    if (bytes <= 0) {
        printf("[ERROR] No response from server!\n");
        printf("\nPress Enter to continue...");
        getchar();
        getchar();
        return;
    }

    printf("[INFO] Received response: %.50s...\n", response);

    if (strncmp(response, "LOGIN_SUCCESS", 13) == 0) {
        if (sscanf(response, "LOGIN_SUCCESS|%d|%[^|]|%lf",
               &current_user_id, current_username, &current_balance) == 3) {

            logged_in = 1;

            printf("\n[SUCCESS] Login successful!\n");
            printf("Welcome, %s! Your balance: %.2f VND\n",
                   current_username, current_balance);

            // Start notification listener
            pthread_t listener_thread;
            if (pthread_create(&listener_thread, NULL, notification_listener, NULL) == 0) {
                pthread_detach(listener_thread);
                printf("[INFO] Notification listener started\n");
            } else {
                printf("[WARNING] Failed to start notification listener\n");
            }
        } else {
            printf("\n[ERROR] Failed to parse login response!\n");
        }
    } else if (strncmp(response, "LOGIN_FAIL", 10) == 0) {
        char error[256];
        if (sscanf(response, "LOGIN_FAIL|%[^\n]", error) == 1) {
            printf("\n[ERROR] Login failed: %s\n", error);
        } else {
            printf("\n[ERROR] Login failed!\n");
        }
    } else {
        printf("\n[ERROR] Invalid response from server!\n");
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

// =====================================================
// AUCTION FEATURE FUNCTIONS
// =====================================================

void list_auctions() {
    print_header("ACTIVE AUCTIONS (IN CURRENT ROOM)");

    if (current_room_id == 0) {
        printf("\n[ERROR] You must join a room first!\n");
        printf("\nPress Enter to continue...");
        getchar();
        getchar();
        return;
    }

    char request[256];
    sprintf(request, "LIST_AUCTIONS|%d\n", current_user_id);
    send_request(request);

    char response[BUFFER_SIZE * 4];
    receive_response(response, BUFFER_SIZE * 4);

    if (strncmp(response, "AUCTION_LIST_FAIL", 17) == 0) {
        char error[256];
        sscanf(response, "AUCTION_LIST_FAIL|%[^\n]", error);
        printf("\n[ERROR] %s\n", error);
    } else if (strncmp(response, "AUCTION_LIST|", 13) == 0) {
        char *data = response + 13;
        char *token = strtok(data, "|");

        int count = 0;
        printf("\n%-5s %-30s %-15s %-15s %-12s %s\n",
               "ID", "Title", "Current Price", "Buy Now", "Time Left", "Bids");
        print_separator();

        while (token != NULL) {
            int id, time_left, total_bids;
            char title[200];
            double current_price, buy_now_price;

            if (sscanf(token, "%d;%[^;];%lf;%lf;%d;%d",
                      &id, title, &current_price, &buy_now_price,
                      &time_left, &total_bids) == 6) {

                int hours = time_left / 3600;
                int minutes = (time_left % 3600) / 60;

                printf("%-5d %-30s %12.2f VND %12.2f VND %3dh %2dm %5d\n",
                       id, title, current_price, buy_now_price,
                       hours, minutes, total_bids);
                count++;
            }

            token = strtok(NULL, "|");
        }

        if (count == 0) {
            printf("No active auctions in this room.\n");
        } else {
            printf("\nTotal: %d auction(s)\n", count);
        }
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

void view_my_auctions() {
    print_header("MY AUCTIONS");

    char request[256];
    sprintf(request, "MY_AUCTIONS|%d\n", current_user_id);
    send_request(request);

    char response[BUFFER_SIZE * 4];
    receive_response(response, BUFFER_SIZE * 4);

    if (strncmp(response, "MY_AUCTIONS|", 12) == 0) {
        char *data = response + 12;
        char *token = strtok(data, "|");

        int count = 0;
        printf("\n%-5s %-30s %-15s %-15s %-12s %-10s %s\n",
               "ID", "Title", "Current Price", "Buy Now", "Time Left", "Status", "Bids");
        print_separator();

        while (token != NULL) {
            int id, time_left, total_bids;
            char title[200], status[20];
            double current_price, buy_now_price;

            if (sscanf(token, "%d;%[^;];%lf;%lf;%d;%[^;];%d",
                      &id, title, &current_price, &buy_now_price,
                      &time_left, status, &total_bids) == 7) {

                int hours = time_left / 3600;
                int minutes = (time_left % 3600) / 60;

                printf("%-5d %-30s %12.2f VND %12.2f VND %3dh %2dm %-10s %5d\n",
                       id, title, current_price, buy_now_price,
                       hours, minutes, status, total_bids);
                count++;
            }

            token = strtok(NULL, "|");
        }

        if (count == 0) {
            printf("You have no auctions.\n");
        } else {
            printf("\nTotal: %d auction(s)\n", count);
        }
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

void view_auction_detail() {
    int auction_id;

    print_header("AUCTION DETAIL");

    printf("Enter Auction ID: ");
    scanf("%d", &auction_id);

    char request[256];
    sprintf(request, "AUCTION_DETAIL|%d|%d\n", auction_id, current_user_id);
    send_request(request);

    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);

    if (strncmp(response, "AUCTION_DETAIL|", 15) == 0) {
        int id, time_left, total_bids;
        char title[200], desc[500], seller[50], status[20];
        double start_price, current_price, buy_now_price, min_increment;

        sscanf(response + 15, "%d|%[^|]|%[^|]|%[^|]|%lf|%lf|%lf|%lf|%d|%[^|]|%d",
               &id, title, desc, seller, &start_price, &current_price,
               &buy_now_price, &min_increment, &time_left, status, &total_bids);

        printf("\n");
        printf("Auction ID       : %d\n", id);
        printf("Title            : %s\n", title);
        printf("Description      : %s\n", desc);
        printf("Seller           : %s\n", seller);
        printf("Starting Price   : %.2f VND\n", start_price);
        printf("Current Price    : %.2f VND\n", current_price);
        printf("Buy Now Price    : %.2f VND\n", buy_now_price);
        printf("Min Bid Increment: %.2f VND\n", min_increment);
        printf("Time Remaining   : %d hours %d minutes\n",
               time_left / 3600, (time_left % 3600) / 60);
        printf("Status           : %s\n", status);
        printf("Total Bids       : %d\n", total_bids);

    } else {
        char error[256];
        sscanf(response, "AUCTION_DETAIL_FAIL|%[^\n]", error);
        printf("\n[ERROR] %s\n", error);
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

void create_auction() {
    char title[200], desc[500];
    double start_price, buy_now_price, min_increment;
    int duration_minutes;

    print_header("CREATE NEW AUCTION");

    if (current_room_id == 0) {
        printf("\n[ERROR] You must join a room first!\n");
        printf("\nPress Enter to continue...");
        getchar();
        getchar();
        return;
    }

    getchar(); // Clear buffer

    printf("Title: ");
    fgets(title, sizeof(title), stdin);
    title[strcspn(title, "\n")] = 0;

    printf("Description: ");
    fgets(desc, sizeof(desc), stdin);
    desc[strcspn(desc, "\n")] = 0;

    printf("Starting Price (VND): ");
    scanf("%lf", &start_price);

    printf("Buy Now Price (VND, 0 for none): ");
    scanf("%lf", &buy_now_price);

    printf("Minimum Bid Increment (VND): ");
    scanf("%lf", &min_increment);

    printf("Duration (minutes): ");
    scanf("%d", &duration_minutes);

    char request[BUFFER_SIZE];
    sprintf(request, "CREATE_AUCTION|%d|%d|%s|%s|%.2f|%.2f|%.2f|%d\n",
            current_user_id, current_room_id, title, desc, start_price,
            buy_now_price, min_increment, duration_minutes);

    send_request(request);

    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);

    if (strncmp(response, "CREATE_AUCTION_SUCCESS", 22) == 0) {
        int auction_id;
        char auction_title[200];
        sscanf(response, "CREATE_AUCTION_SUCCESS|%d|%[^\n]",
               &auction_id, auction_title);

        printf("\n[SUCCESS] Auction created successfully!\n");
        printf("Auction ID: %d\n", auction_id);
        printf("Title: %s\n", auction_title);
        printf("Duration: %d minutes\n", duration_minutes);
        printf("Room: %s (ID: %d)\n", current_room_name, current_room_id);
    } else {
        char error[256];
        sscanf(response, "CREATE_AUCTION_FAIL|%[^\n]", error);
        printf("\n[ERROR] Failed to create auction: %s\n", error);
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

void place_bid() {
    int auction_id;
    double bid_amount;

    print_header("PLACE BID");

    if (current_room_id == 0) {
        printf("\n[ERROR] You must join a room first!\n");
        printf("\nPress Enter to continue...");
        getchar();
        getchar();
        return;
    }

    printf("Auction ID: ");
    scanf("%d", &auction_id);

    printf("Bid Amount (VND): ");
    scanf("%lf", &bid_amount);

    char request[256];
    sprintf(request, "PLACE_BID|%d|%d|%.2f\n",
            auction_id, current_user_id, bid_amount);

    send_request(request);

    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);

    if (strncmp(response, "BID_SUCCESS", 11) == 0) {
        int aid, total_bids, time_left;
        double amount;
        sscanf(response, "BID_SUCCESS|%d|%lf|%d|%d",
               &aid, &amount, &total_bids, &time_left);

        printf("\n╔════════════════════════════════════════════════════════╗\n");
        printf("║          ✅ BID PLACED SUCCESSFULLY! ✅                ║\n");
        printf("╠════════════════════════════════════════════════════════╣\n");
        printf("║ Auction ID:      #%-5d                              ║\n", aid);
        printf("║ Your Bid:        %12.2f VND                   ║\n", amount);
        printf("║ Your Rank:       #%-3d (Current Winner!)               ║\n", total_bids);

        if (time_left < 30 && time_left > 0) {
            printf("║ ⚠️  WARNING:      Only %2d seconds left!              ║\n", time_left);
            printf("║ Time extended to 30 seconds!                           ║\n");
        } else {
            printf("║ Time Left:       %3d minutes                           ║\n", time_left / 60);
        }

        printf("╚════════════════════════════════════════════════════════╝\n");

        current_balance -= amount;

    } else {
        char error[256];
        sscanf(response, "BID_FAIL|%[^\n]", error);
        printf("\n[ERROR] Bid failed: %s\n", error);
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

void buy_now() {
    int auction_id;

    print_header("BUY NOW");

    if (current_room_id == 0) {
        printf("\n[ERROR] You must join a room first!\n");
        printf("\nPress Enter to continue...");
        getchar();
        getchar();
        return;
    }

    printf("Auction ID: ");
    scanf("%d", &auction_id);

    printf("\nAre you sure you want to buy this auction immediately? (y/n): ");
    char confirm;
    scanf(" %c", &confirm);

    if (confirm != 'y' && confirm != 'Y') {
        printf("Buy now cancelled.\n");
        printf("\nPress Enter to continue...");
        getchar();
        getchar();
        return;
    }

    char request[256];
    sprintf(request, "BUY_NOW|%d|%d\n", auction_id, current_user_id);

    send_request(request);

    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE);

    if (strncmp(response, "BUY_NOW_SUCCESS", 15) == 0) {
        int aid;
        sscanf(response, "BUY_NOW_SUCCESS|%d", &aid);

        printf("\n[SUCCESS] Purchase successful!\n");
        printf("You won auction #%d\n", aid);

    } else {
        char error[256];
        sscanf(response, "BUY_NOW_FAIL|%[^\n]", error);
        printf("\n[ERROR] Purchase failed: %s\n", error);
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

void view_bid_history() {
    int auction_id;

    print_header("BID HISTORY");

    printf("Auction ID: ");
    scanf("%d", &auction_id);

    char request[256];
    sprintf(request, "BID_HISTORY|%d|%d\n", auction_id, current_user_id);
    send_request(request);

    char response[BUFFER_SIZE * 2];
    receive_response(response, BUFFER_SIZE * 2);

    if (strncmp(response, "BID_HISTORY_FAIL", 16) == 0) {
        char error[256];
        sscanf(response, "BID_HISTORY_FAIL|%[^\n]", error);
        printf("\n[ERROR] %s\n", error);
    } else if (strncmp(response, "BID_HISTORY|", 12) == 0) {
        char *data = response + 12;
        char *token = strtok(data, "|");

        printf("\n%-20s %-15s %s\n", "Bidder", "Amount", "Time");
        print_separator();

        int count = 0;
        while (token != NULL) {
            char bidder[50], time_str[64];
            double amount;

            if (sscanf(token, "%[^;];%lf;%[^\n]", bidder, &amount, time_str) == 3) {
                printf("%-20s %12.2f VND %s\n", bidder, amount, time_str);
                count++;
            }

            token = strtok(NULL, "|");
        }

        if (count == 0) {
            printf("No bids found for this auction.\n");
        } else {
            printf("\nTotal: %d bid(s)\n", count);
        }
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

void view_auction_history() {
    print_header("AUCTION HISTORY (COMPLETED AUCTIONS)");

    char request[256];
    sprintf(request, "AUCTION_HISTORY|%d\n", current_user_id);
    send_request(request);

    char response[BUFFER_SIZE * 4];
    receive_response(response, BUFFER_SIZE * 4);

    if (strncmp(response, "AUCTION_HISTORY|", 16) == 0) {
        char *data = response + 16;
        char *token = strtok(data, "|");

        int count = 0;
        printf("\n%-5s %-35s %-15s %-20s %s\n",
               "ID", "Title", "Final Price", "Winner", "Method");
        print_separator();

        while (token != NULL) {
            int id;
            char title[200], winner[50], method[20];
            double final_price;

            if (sscanf(token, "%d;%[^;];%lf;%[^;];%s",
                      &id, title, &final_price, winner, method) == 5) {

                const char *method_display;
                if (strcmp(method, "buy_now") == 0) {
                    method_display = "Buy Now 🛒";
                } else if (strcmp(method, "bid") == 0) {
                    method_display = "Bidding 🔨";
                } else {
                    method_display = "No Bids ❌";
                }

                printf("%-5d %-35s %12.2f VND %-20s %s\n",
                       id, title, final_price, winner, method_display);
                count++;
            }

            token = strtok(NULL, "|");
        }

        if (count == 0) {
            printf("No completed auctions found.\n");
        } else {
            printf("\nTotal: %d completed auction(s)\n", count);
        }
    }

    printf("\nPress Enter to continue...");
    getchar();
    getchar();
}

// =====================================================
// MAIN FUNCTION
// =====================================================

int main() {
    struct sockaddr_in server_addr;

    // Create socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        exit(EXIT_FAILURE);
    }

    // Connect to server
    printf("Connecting to server %s:%d...\n", SERVER_IP, SERVER_PORT);

    if (connect(client_socket, (struct sockaddr*)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected successfully!\n");
    sleep(1);

    // Main loop
    while (running) {
        clear_screen();

        if (!logged_in) {
            display_main_menu();

            int choice;
            scanf("%d", &choice);

            switch (choice) {
                case 1:
                    register_user();
                    break;
                case 2:
                    login_user();
                    break;
                case 3:
                    printf("Goodbye!\n");
                    running = 0;
                    break;
                default:
                    printf("Invalid option!\n");
                    sleep(1);
            }
        } else {
            display_user_menu();

            int choice;
            scanf("%d", &choice);

            switch (choice) {
                case 1:
                    create_room();
                    break;
                case 2:
                    list_rooms();
                    break;
                case 3:
                    join_room();
                    break;
                case 4:
                    leave_room();
                    break;
                case 5:
                    view_room_detail();
                    break;
                case 6:
                    view_my_room();
                    break;
                case 7:
                    list_auctions();
                    break;
                case 8:
                    view_my_auctions();
                    break;
                case 9:
                    view_auction_detail();
                    break;
                case 10:
                    create_auction();
                    break;
                case 11:
                    place_bid();
                    break;
                case 12:
                    buy_now();
                    break;
                case 13:
                    view_bid_history();
                    break;
                case 14:
                    view_auction_history();
                    break;
                case 15:
                    // Leave room before logout
                    if (current_room_id > 0) {
                        char request[256];
                        sprintf(request, "LEAVE_ROOM|%d\n", current_user_id);
                        send_request(request);
                    }

                    logged_in = 0;
                    current_user_id = 0;
                    current_room_id = 0;
                    memset(current_username, 0, sizeof(current_username));
                    strcpy(current_room_name, "None");
                    current_balance = 0.0;
                    printf("Logged out successfully!\n");
                    sleep(1);
                    break;
                default:
                    printf("Invalid option!\n");
                    sleep(1);
            }
        }
    }

    // Cleanup
    send_request("QUIT|\n");
    close(client_socket);

    return 0;
}
