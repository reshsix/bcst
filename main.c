/*
This file is part of bcst.

bcst is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published
by the Free Software Foundation, version 3.

bcst is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with bcst. If not, see <https://www.gnu.org/licenses/>.
*/

#include <errno.h>
#include <stdio.h>
#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <unistd.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#define STR_ESOCKET strerror(errno)
#define STR_ENOMEM  strerror(ENOMEM)

static void *
nfree(void *mem)
{
    free(mem);
    return NULL;
}

static void *
realloc_s(void *mem, size_t size)
{
    void *ret = realloc(mem, size);
    return (ret) ? ret : nfree(mem);
}

static void *
realloc_f(void *mem, size_t count, size_t *size)
{
    void *ret = mem;

    size_t size2 = *size;
    while (count >= size2)
        size2 *= 2;

    if (size2 != *size)
    {
        ret = realloc_s(ret, size2);
        *size = size2;
    }

    return ret;
}

static int
socket_pub(const char *path)
{
    int ret = socket(AF_UNIX, SOCK_STREAM, 0);

    if (ret >= 0)
    {
        struct sockaddr_un addr = {.sun_family = AF_UNIX};

        if (strlen(path) > sizeof(addr.sun_path) ||
            !strcpy(addr.sun_path, path) ||
            bind(ret, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            close(ret);
            ret = -1;
        }
    }

    if (ret >= 0 && listen(ret, 128) < 0)
    {
        close(ret);
        unlink(path);
        ret = -1;
    }

    return ret;
}

static int
socket_sub(const char *path)
{
    int ret = socket(AF_UNIX, SOCK_STREAM, 0);

    if (ret >= 0)
    {
        struct sockaddr_un addr = {.sun_family = AF_UNIX};

        if (strlen(path) > sizeof(addr.sun_path) ||
            !strcpy(addr.sun_path, path) ||
            connect(ret, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            close(ret);
            ret = -1;
        }
    }

    return ret;
}

static bool
error(const char *s)
{
    fprintf(stderr, "bcst: %s\n", s);
    return false;
}

static bool running = true;
static void
handler(int _)
{
    (void)_;
    running = false;
}

static bool
publish(const char *path)
{
    bool ret = true;

    int s = socket_pub(path);
    if (s < 0)
        ret = error(STR_ESOCKET);

    char *buf = NULL;
    size_t buf_c = 0, buf_s = 128;
    if (ret && !(buf = malloc(buf_s)))
        ret = error(STR_ENOMEM);

    int *subs = NULL;
    size_t subs_c = 0, subs_s = sizeof(int) * 32;
    if (ret && !(subs = malloc(subs_s)))
        ret = error(STR_ENOMEM);

    while (ret && running)
    {
        int in = STDIN_FILENO;
        struct pollfd pfds[2] = {[0] = {.fd = in, .events = POLLIN | POLLHUP},
                                 [1] = {.fd = s,  .events = POLLIN}};
        if (poll(pfds, 2, -1) >= 0)
        {
            if (pfds[0].revents & POLLIN)
            {
                size_t bytes = 0;
                if (ioctl(STDIN_FILENO, FIONREAD, &bytes) == 0 && bytes != 0)
                {
                    buf = realloc_f(buf, buf_c + bytes, &buf_s);
                    read(STDIN_FILENO, &(buf[buf_c]), bytes);
                    buf_c += bytes;

                    while (true)
                    {
                        char *end = memchr(buf, '\n', buf_c);
                        if (!end)
                            break;

                        size_t length = end - buf + 1;
                        for (size_t i = 0; i < subs_c / sizeof(int); i++)
                        {
                            if (subs[i] < 0)
                                continue;

                            if (!send(subs[i], buf, length, MSG_NOSIGNAL))
                            {
                                close(subs[i]);
                                subs[i] = -1;
                            }
                        }

                        buf_c -= length;
                        memmove(buf, &(end[1]), buf_c);
                        if (buf_c == 0)
                            break;
                    }
                }
                else
                {
                    running = false;
                    break;
                }
            }
            else if (pfds[0].revents & POLLHUP)
                break;

            if (pfds[1].revents & POLLIN)
            {
                int new = accept(s, NULL, NULL);
                if (new >= 0)
                {
                    bool found = false;
                    size_t count = subs_c / sizeof(int);
                    for (size_t i = 0; i < count; i++)
                    {
                        if (subs[i] < 0)
                        {
                            subs[i] = new;
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                    {
                        subs[count] = new;
                        subs = realloc_f(subs, subs_c += sizeof(int), &subs_s);
                        if (!subs)
                            ret = error(STR_ENOMEM);
                    }
                }
            }
        }
    }

    if (s >= 0)
    {
        close(s);
        unlink(path);
    }

    free(buf);
    for (size_t i = 0; i < subs_c / sizeof(int); i++)
    {
        if (subs[i] >= 0)
            close(subs[i]);
    }
    free(subs);

    return ret;
}

static bool
subscribe(const char *path)
{
    bool ret = true;

    int s = socket_sub(path);
    if (s < 0)
        ret = error(STR_ESOCKET);

    char *buf = NULL;
    size_t buf_c = 0, buf_s = 128;
    if (ret && !(buf = malloc(buf_s)))
        ret = error(STR_ENOMEM);

    while (ret && running)
    {
        struct pollfd pfds[1] = {[0] = {.fd = s, .events = POLLIN}};
        if (poll(pfds, 1, -1) >= 0)
        {
            if (pfds[0].revents & POLLIN)
            {
                size_t bytes = 0;
                if (ioctl(s, FIONREAD, &bytes) == 0 && bytes != 0)
                {
                    buf = realloc_f(buf, buf_c + bytes, &buf_s);
                    read(s, &(buf[buf_c]), bytes);
                    buf_c += bytes;

                    while (true)
                    {
                        char *end = memchr(buf, '\n', buf_c);
                        if (!end)
                            break;

                        size_t length = end - buf + 1;
                        write(STDOUT_FILENO, buf, length);

                        buf_c -= length;
                        memmove(buf, &(end[1]), buf_c);
                        if (buf_c == 0)
                            break;
                    }
                }
                else
                    running = false;
            }
        }
    }

    if (s >= 0)
        close(s);
    free(buf);

    return ret;
}

static bool
usage(void)
{
    fprintf(stderr, "bcst pub/sub FILE\n");
    fprintf(stderr, "Broadcasts data to multiple listeners\n");
    return false;
}

extern int
main(int argc, const char* argv[])
{
    bool ret = true;

    setlocale(LC_ALL, "");
    signal(SIGINT, handler);

    if (argc != 3)
        ret = usage();

    if (ret)
    {
        if (strcmp(argv[1], "pub") == 0)
            ret = publish(argv[2]);
        else if (strcmp(argv[1], "sub") == 0)
            ret = subscribe(argv[2]);
        else
            ret = usage();
    }

    return (ret) ? EXIT_SUCCESS : EXIT_FAILURE;
}
