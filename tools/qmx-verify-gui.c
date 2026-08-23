/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define OUTPUT_LIMIT (64 * 1024)

static void show_message(Uint32 flags, const char *title, const char *message)
{
    if (SDL_ShowSimpleMessageBox(flags, title, message, NULL) < 0) {
        fprintf(stderr, "%s: %s\n", title, message);
    }
}

static char *read_all(int fd)
{
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);

    if (!buf) {
        return NULL;
    }

    for (;;) {
        ssize_t n;

        if (len + 2048 + 1 > cap) {
            size_t next = cap * 2;
            char *tmp;

            if (next > OUTPUT_LIMIT) {
                next = OUTPUT_LIMIT;
            }
            if (next <= cap) {
                break;
            }
            tmp = realloc(buf, next);
            if (!tmp) {
                free(buf);
                return NULL;
            }
            buf = tmp;
            cap = next;
        }

        n = read(fd, buf + len, cap - len - 1);
        if (n > 0) {
            len += (size_t)n;
            if (len >= OUTPUT_LIMIT - 1) {
                break;
            }
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        free(buf);
        return NULL;
    }

    buf[len] = '\0';
    return buf;
}

int main(int argc, char **argv)
{
    int pipefd[2];
    pid_t pid;
    int status;
    char *output;

    if (argc != 2) {
        show_message(SDL_MESSAGEBOX_ERROR,
                     "QMX Verification Error",
                     "Exactly one QMX configuration file must be selected.");
        return EXIT_FAILURE;
    }

    if (pipe(pipefd) < 0) {
        show_message(SDL_MESSAGEBOX_ERROR,
                     "QMX Verification Error",
                     "Unable to create validation output pipe.");
        return EXIT_FAILURE;
    }

    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        show_message(SDL_MESSAGEBOX_ERROR,
                     "QMX Verification Error",
                     "Unable to start QEMU-QMX validator.");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
            dup2(pipefd[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(pipefd[1]);
        execlp("qemu-system-x86_64",
               "qemu-system-x86_64",
               "-qmx-check",
               argv[1],
               (char *)NULL);
        _exit(errno == ENOENT ? 127 : 126);
    }

    close(pipefd[1]);
    output = read_all(pipefd[0]);
    close(pipefd[0]);

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            status = -1;
            break;
        }
    }

    if (!output) {
        output = strdup("Unable to read validation output.");
    }

    if (status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        char message[PATH_MAX + 128];
        snprintf(message, sizeof(message),
                 "%s\n\nQMX configuration is valid.", argv[1]);
        show_message(SDL_MESSAGEBOX_INFORMATION,
                     "QMX Configuration Valid",
                     message);
        free(output);
        return EXIT_SUCCESS;
    }

    if (!output || !output[0]) {
        free(output);
        output = strdup("QEMU-QMX validation failed without diagnostic output.");
    }

    show_message(SDL_MESSAGEBOX_ERROR,
                 "QMX Configuration Error",
                 output ? output : "QEMU-QMX validation failed.");
    free(output);
    return EXIT_FAILURE;
}
