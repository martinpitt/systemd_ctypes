/*
 * Compile with: gcc -o inotify_cancel inotify_cancel.c -lsystemd && ./inotify_cancel
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include <systemd/sd-event.h>

static int inotify_handler(sd_event_source *s, const struct inotify_event *event, void *userdata) {
    printf("Inotify event: mask=0x%x name=%s\n", event->mask, event->name);
    return 0;
}

static void touch_file() {
    printf("Creating test file in top to trigger events...\n");
    int fd = open("/tmp/work/test_file", O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        fprintf(stderr, "Failed to create test file: %m\n");
        exit(1);
    }
    write(fd, "test", 4);
    close(fd);
    puts("Created test file");
}

int main() {
    sd_event *event_loop = NULL;
    sd_event_source *i_top = NULL;
    sd_event_source *i_sub = NULL;

    int ret;

    puts("Creating systemd event loop...");
    ret = sd_event_new(&event_loop);
    if (ret < 0) {
        fprintf(stderr, "Failed to create event loop: %s\n", strerror(-ret));
        return 1;
    }

    /* create /tmp/work directory if it doesn't exist */
    ret = mkdir("/tmp/work", 0755);
    if (ret < 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create /tmp/work directory: %m\n");
        return 1;
    }

    unlink("/tmp/work/test_file"); // Clean up any existing test file

    puts("Setting up inotify sources...");

    // Create inotify sources for top directory
    ret = sd_event_add_inotify(event_loop, &i_top, "/tmp/work",
                               IN_CREATE | IN_DELETE | IN_MOVE | IN_MODIFY,
                               inotify_handler, NULL);
    if (ret < 0) {
        fprintf(stderr, "Failed to add top inotify source: %s\n", strerror(-ret));
        return 1;
    }
    puts("Added inotify source for /tmp/work");

    // Create inotify source underneath top (fails)
    ret = sd_event_add_inotify(event_loop, &i_sub, "/tmp/work/nonexisting",
                               IN_CREATE | IN_DELETE | IN_MOVE,
                               inotify_handler, NULL);
    if (ret != -ENOENT) {
        fprintf(stderr, "Failed to add sub inotify source: %s\n", strerror(-ret));
        return 1;
    }
    puts("Tried to add inotify source for /tmp/work/nonexisting");

    puts("Running event loop briefly to establish sources...");
    ret = sd_event_run(event_loop, 100000); // 100ms
    if (ret < 0) {
        fprintf(stderr, "Event loop run failed: %s\n", strerror(-ret));
        return 1;
    }

     puts("Cancelling first inotify source...");
    sd_event_source_unref(i_top);
    i_top = NULL;

    ret = sd_event_run(event_loop, 100000); // 100ms

    // Create the test file before starting the loop so events are already queued
    touch_file();

    puts("Running event loop to process inotify events...");
    // Clean room implementation based on libsystemd documentation

    for (int i = 0; i < 5; i++) {
        printf("=== Iteration %i ===\n", i);

        // Check current event loop state and act accordingly
        int state = sd_event_get_state(event_loop);
        printf("Event loop state: %d\n", state);

        switch (state) {
            case SD_EVENT_INITIAL:
                // Step 1: Prepare - check for pending events and arm timers
                ret = sd_event_prepare(event_loop);
                if (ret < 0) {
                    fprintf(stderr, "sd_event_prepare() failed: %s\n", strerror(-ret));
                    return 1;
                }
                printf("sd_event_prepare() returned %i\n", ret);
                break;

            case SD_EVENT_ARMED:
                ret = sd_event_wait(event_loop, 100000); // 100ms timeout
                if (ret < 0) {
                    fprintf(stderr, "sd_event_wait() failed: %s\n", strerror(-ret));
                    return 1;
                }
                printf("sd_event_wait() returned %i\n", ret);
                break;

            case SD_EVENT_PENDING:
                ret = sd_event_dispatch(event_loop);
                if (ret < 0) {
                    fprintf(stderr, "sd_event_dispatch() failed: %s\n", strerror(-ret));
                    return 1;
                }
                printf("sd_event_dispatch() returned %i\n", ret);

            case SD_EVENT_PREPARING:
            case SD_EVENT_RUNNING:
            case SD_EVENT_EXITING:
                // These states are handled by the event loop internally
                break;

            default:
                fprintf(stderr, "Unexpected event loop state: %d\n", state);
                return 1;
        }

        printf("End of iteration %i\n\n", i);
    }

    printf("Cleaning up...\n");
    if (i_sub)
        sd_event_source_unref(i_sub);
    sd_event_unref(event_loop);

    puts("success");
    return 0;
}
