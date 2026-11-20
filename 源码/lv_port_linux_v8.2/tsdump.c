#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

int main(void){
    int fd = open("/dev/input/event0", O_RDONLY);
    if(fd < 0){ perror("open"); return 1; }
    struct input_event ev;
    while(read(fd, &ev, sizeof ev) > 0){
        if(ev.type == EV_ABS){
            printf("code=%d value=%d\n", ev.code, ev.value);
            fflush(stdout);
        }
    }
    return 0;
}
