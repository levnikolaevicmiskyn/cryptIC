#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define MAX_MESSAGE_SIZE 512
#define TERM '\n'

int sendMessage(int fd, const char *buf, ssize_t size) {
    printf("Sending message '%s[\\n]' [%lu B]... ", buf, size + 1);
    fflush(stdout);
    char *terminated_message = (char *) malloc((size + 1) * sizeof(char));
    strcpy(terminated_message, buf);
    terminated_message[size] = TERM;
    int n_sent_bytes = write(fd, terminated_message, size + 1);
    if (n_sent_bytes < 0) {
        printf("\n");
        perror("Error in sending data");
        return n_sent_bytes;
    }
    printf("Done [%d B]!\n", n_sent_bytes);
    free(terminated_message);
    return n_sent_bytes;
}

int readMessage(int fd, char *buf, ssize_t size) {
    printf("Awaiting response... ");
    fflush(stdout);
    int n_bytes_read = read(fd, buf, size);
    if (n_bytes_read < 0) {
        printf("\n");
        perror("Error in read");
        return n_bytes_read;
    }
    buf[n_bytes_read] = '\0';
    printf("Response [%d B]: %s\n", n_bytes_read, buf);
    return n_bytes_read;
}

int main(int argc, char **argv) {
    // Check for correct arguments
    if (argc < 2) {
        fprintf(stderr, "Not enough input argument: specify device file name\n");
        exit(-1);
    }
    char *fname = argv[1];
    // Open device file
    int fd = open(fname, O_RDWR | O_DSYNC);
    if (fd < 0) {
        perror("Could not open requested device");
        exit(-2);
    }
    // Start communication
    int done = 0;
    char message[MAX_MESSAGE_SIZE] = "\0";
    while (!done) {
        printf("> ");
        scanf("%512s", message);
        if (strcmp(message, "exit") == 1) {
            done = 1;
        }
        // Send data
        int n_sent_bytes = sendMessage(fd, message, strlen(message));
        if (n_sent_bytes > 0) {
            sleep(1);
            readMessage(fd, message, 2);
        }
    }
    printf("Quitting\n");
    // Close file
    close(fd);
    return 0;
}
