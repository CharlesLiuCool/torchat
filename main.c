#include "backend.h"
#include <stdio.h>
#include <string.h>

/* Defined in UI/main_ui.c */
void run_ui(void);
void ui_on_msg(int fd, const char* msg);
void ui_on_peer_connected(int fd);
void ui_on_peer_disconnected(int fd);

int main(void)
{
    char nickname[32] = {0};
    int  my_port      = 0;

    printf("Enter nickname: ");
    if (!fgets(nickname, sizeof(nickname), stdin)) return 1;
    nickname[strcspn(nickname, "\n")] = '\0';
    if (nickname[0] == '\0') { fprintf(stderr, "Nickname cannot be empty\n"); return 1; }

    printf("Enter listen port (0 to skip): ");
    if (scanf("%d", &my_port) != 1) return 1;
    getchar(); /* consume newline */

    backend_init(nickname,
                 ui_on_msg,
                 ui_on_peer_connected,
                 ui_on_peer_disconnected);

    if (my_port > 0) {
        if (backend_start_listening(my_port) < 0) {
            fprintf(stderr, "Failed to listen on port %d\n", my_port);
            return 1;
        }
    }

    run_ui();       /* blocks until window closed; calls backend_shutdown() */
    return 0;
}