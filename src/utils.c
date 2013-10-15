/*
 * utils.c
 */

#include "utils.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>

#define INITIAL_BUF_SIZE 4096

const char**
read_nul_sep(const char* filename)
{
    char *buf = NULL;
    char *newbuf = NULL;
    int buflen = 0;
    int count = 0;
    int size = 0;
    int i = 0;
    char **results;
    int fd = open(filename, O_RDONLY);

    buf = (char*)malloc(INITIAL_BUF_SIZE);
    size = INITIAL_BUF_SIZE;
    if( buf == NULL )
    {
        close(fd);
        return NULL;
    }

    /* Read the full file. */
    while( 1 )
    {
        int r = 0;

        if( size-buflen == 0 )
        {
            buf = (char*)realloc(buf, size*2);
            size = size*2;
            if( buf == NULL )
            {
                close(fd);
                return NULL;
            }
        }

        r = read(fd, &buf[buflen], size-buflen);
        if( r < 0 )
        {
            close(fd);
            free(buf);
            return NULL;
        }

        if( r == 0 )
        {
            close(fd);
            break;
        }

        buflen += r;
    }

    /* Count nuls. */
    for( i = 0; i < buflen; i += 1 )
    {
        if( buf[i] == '\0' )
        {
            count += 1;
        }
    }
    if( buf[i-1] != '\0' )
    {
        count += 1;
    }

    /* Allocate our strings. */
    results = (char**)malloc(sizeof(char*)*(count+1) + (buflen+1));
    for( i = count; i < count + 1; i += 1 )
    {
        results[i] = NULL;
    }

    /* Copy buffer in and reset the pointer. */
    newbuf = ((char*)results) + sizeof(char*)*(count+1);
    memcpy(newbuf, buf, buflen);
    newbuf[buflen] = '\0';
    free(buf);

    /* Point the strings into the new buffer. */
    count = 0;
    for( i = 0; i < buflen; )
    {
        results[count++] = &newbuf[i];
        for( ; i < buflen && newbuf[i] != '\0'; i += 1 );
        i += 1;
    }

    return (const char**)results;
}

const char*
read_link(const char* filename)
{
    char buf[PATH_MAX+1];
    size_t r = readlink(filename, buf, PATH_MAX+1);
    if( r == (size_t)-1 )
    {
        return NULL;
    }
    buf[r] = '\0';
    return (const char*)strdup(buf);
}

pid_t*
get_tasks(void)
{
    int count = 0;
    int size = 3;
    pid_t *buffer = malloc(sizeof(pid_t) * size);
    DIR *dp = NULL;
    struct dirent *ep = NULL;
    buffer[0] = (pid_t)-1;

    dp = opendir("/proc/self/task");
    if( dp == NULL )
    {
        fprintf(stderr, "Failed to fetch tasks?\n");
        return buffer;
    }

    while( (ep = readdir(dp)) != NULL )
    {
        if( ep->d_name[0] == '.' || ep->d_name[0] == '\0' )
        {
            continue;
        }
        long task_id = strtol(ep->d_name, NULL, 10);
        if( count+1 >= size )
        {
            size = size * 2;
            buffer = realloc(buffer, sizeof(long) * size);
        }
        buffer[count] = (pid_t)task_id;
        count += 1;
        buffer[count] = (pid_t)-1;
    }

    return buffer;
}
