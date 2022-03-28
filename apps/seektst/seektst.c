#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    int fd;
    if ((fd = open("byte.txt", O_RDWR)) == -1) {
        perror("open");
        exit(1);
    }
    fflush(NULL);
    if (write(fd, "See you", 7) != 7) {
        perror("write");
        exit(1);
    }
    lseek(fd, 5, SEEK_END);
    if (write(fd, " later!", 7) != 7) {
        perror("write");
        exit(1);
    }
    if (write(fd, "\nBye!\n", 6) != 6) {
        perror("write");
        exit(1);
    }
    lseek(fd, 7, SEEK_SET);
    char buf[10];
    if (read(fd, buf, 7) != 7) {
        perror("read");
        exit(1);
    }
    printf("readed: '%s'", buf);
    if (write(fd, "\nhoge\n", 6) != 6) {
        perror("write");
        exit(1);
    }
    if (close(fd) == 1) {
        perror("close");
        exit(1);
    }
    return 0;
}
