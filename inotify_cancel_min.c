/*
gcc -o inotify_cancel_min inotify_cancel_min.c -lsystemd && ./inotify_cancel_min
*/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <systemd/sd-event.h>

static int inotify_handler(sd_event_source *s, const struct inotify_event *event, void *userdata) {
    printf("Inotify event: mask=0x%x name=%s\n", event->mask, event->name);
    return 0;
}

static void touch_file() {
    printf("Creating test file to trigger events...\n");
    int fd = open("/tmp/work/test_file", O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        fprintf(stderr, "Failed to create test file: %m\n");
        exit(1);
    }
    write(fd, "test", 4);
    close(fd);
}

int main() {
    sd_event *event_loop = NULL;
    sd_event_source *i_first = NULL;
    sd_event_source *i_second = NULL;

    int ret;

    /* create and clean /tmp/work directory */
    ret = mkdir("/tmp/work", 0755);
    if (ret < 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create /tmp/work directory: %m\n");
        return 1;
    }
    unlink("/tmp/work/test_file");

    puts("Creating systemd event loop...");
    ret = sd_event_new(&event_loop);
    if (ret < 0) {
        fprintf(stderr, "Failed to create event loop: %s\n", strerror(-ret));
        return 1;
    }

    puts("Setting up inotify sources..");
    ret = sd_event_add_inotify (event_loop, &i_first, "/tmp/work",
                                IN_CREATE | IN_MODIFY, inotify_handler, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to add top inotify source: %s\n", strerror(-ret));
        return 1;
    }

    // Create second source on the same directory
    ret = sd_event_add_inotify(event_loop, &i_second, "/tmp/work",
                               IN_CREATE, inotify_handler, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to add sub inotify source: %s\n", strerror(-ret));
        return 1;
    }

    /* works when commenting this out */
    puts("Cancelling first inotify source...");
    sd_event_source_unref(i_first);

    /* Create the test file before starting the loop so events are already queued */
    touch_file();

    /* Run event loop several times to pick up all events */
    for (int i = 1; i < 5; i++) {
        printf("Running event loop cycle %i to process inotify events...\n", i);
        ret = sd_event_run(event_loop, 1000000); // 1s
        if (ret < 0) {
            fprintf(stderr, "Failed to run event loop #%i: %s\n", i, strerror(-ret));
            return 1;
        }
    }

    puts("success");
    return 0;
}
