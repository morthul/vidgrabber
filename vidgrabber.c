#include <netinet/in.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <syslog.h>

int connect_nonb(int sockfd, const struct sockaddr *saptr, socklen_t salen, int nsec);
char *grabDataWithURL(int sockfd, const char *URL, const char *ip, size_t *inlen);
int connectToIP(const char *ip);
void *camera_loop(void *ipPointer);


int connect_nonb(int sockfd, const struct sockaddr *saptr, socklen_t salen, int nsec)
{
    int flags, n, error;
    socklen_t len;
    fd_set rset, wset;
    struct timeval tval;

    flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    error = 0;
    if ( (n = connect(sockfd, saptr, salen)) < 0)
        if (errno != EINPROGRESS)
            return(-1);

    /* Do whatever we want while the connect is taking place. */

    if (n == 0)
        goto done;/* connect completed immediately */

    FD_ZERO(&rset);
    FD_SET(sockfd, &rset);
    wset = rset;
    tval.tv_sec = nsec;
    tval.tv_usec = 0;

    if ( (n = select(sockfd+1, &rset, &wset, NULL,
                     nsec ? &tval : NULL)) == 0) {
        close(sockfd);/* timeout */
        errno = ETIMEDOUT;
        return(-1);
    }

    if (FD_ISSET(sockfd, &rset) || FD_ISSET(sockfd, &wset)) {
        len = sizeof(error);
        if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
            return(-1);/* Solaris pending error */
    } else
        assert(0 && "select error: sockfd not set");

 done:
    fcntl(sockfd, F_SETFL, flags);/* restore file status flags */

    if (error) {
        close(sockfd);/* just in case */
        errno = error;
        return(-1);
    }
    return(0);
}

char *grabDataWithURL(int sockfd, const char *URL, const char *ip, size_t *inlen) {
    fd_set rset, wset;
    FD_ZERO(&rset);
    FD_SET(sockfd, &rset);
    wset = rset;
    int n = 0;
    struct timeval tval;
    tval.tv_sec = 1;
    tval.tv_usec = 0;
    if ( (n = select(sockfd+1, NULL, &wset, NULL, &tval)) == 0) {
        fprintf(stderr, "Failed to get socket ready!\n");
        return NULL;
    }
    if (FD_ISSET(sockfd, &wset)) {
        // Ready to write, let's ask for motion.
        char writeData[1024] = {0};
        sprintf(writeData, "GET %s HTTP/1.1\r\nUser-Agent: DKR/1.0\r\nHost: %s\r\nAccept: */*\r\n\r\n", URL, ip);
        write(sockfd, writeData, strlen(writeData));
    }
#define INITIAL_BUF_SIZE 4096
    char *outBuf = calloc(INITIAL_BUF_SIZE, sizeof(char));
    size_t walker = 0;
    size_t curBufLength = INITIAL_BUF_SIZE;
    while (1) {
        FD_ZERO(&rset);
        FD_SET(sockfd, &rset);
        tval.tv_sec = 1;
        tval.tv_usec = 0;
        if ((n = select(sockfd+1, &rset, NULL, NULL, &tval)) == 0) {
            fprintf(stderr, "Failed to get a response for %s on %s!\n", URL, ip);
            close(sockfd);
            return NULL;
        }
        char readBytes[INITIAL_BUF_SIZE] = {0};
        ssize_t slen = read(sockfd, readBytes, sizeof(readBytes));
        if (slen <= 0) {
            close(sockfd);
            if (inlen) *inlen = walker;
            return outBuf;
        } else {
            size_t len = (size_t)slen; // convert to unsigned, we know greater than zero
            if ((walker + len) > curBufLength) {
                while (curBufLength < (walker + len)) {
                    curBufLength *= 2;
                }
                char *newBuf = realloc(outBuf, curBufLength);
                if (!newBuf) {
                    fprintf(stderr, "Failed to reallocate to size %zu\n", curBufLength);
                    free(outBuf);
                    return NULL;
                }
                outBuf = newBuf;
            }
            memcpy(outBuf + walker, readBytes, len);
            walker += len;
        }
    }
    return NULL;
}

int connectToIP(const char *ip) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(sockfd >= 0);
    struct sockaddr_in      addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_port = htons(80);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = 0; // XXX _addr.sin_addr.s_addr;
    inet_pton(AF_INET, ip, &addr.sin_addr);
    int err = connect_nonb(sockfd, (const struct sockaddr *)&addr, sizeof(addr), 5);
    // int err = connect(sockfd, (const struct sockaddr *)&addr, sizeof(addr));
    if (err != 0) {
        fprintf(stderr, "Error with sockies: %d.\n", errno);
        return -1;
    }
    assert(err  == 0);

    int flags = fcntl(sockfd, F_GETFL);
    err = fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    assert(err == 0);
    return sockfd;
}

static char *check_for_motion(const char *ip) {
    int sockfd = connectToIP(ip);
    if (sockfd >= 0) {
        return grabDataWithURL(sockfd, "/get?mdresult", ip, NULL);
    } else {
        return NULL;
    }
}

static char *captureSnapshot(const char *ip, size_t *len) {
    int sockfd = connectToIP(ip);
    if (sockfd >= 0) {
        return grabDataWithURL(sockfd, "/image?res=full&quality=20&x0=0&y0=0&x1=2048&y1=1536", ip, len);
    } else {
        return NULL;
    }
}

static char *startOfDataForHTTPRequest(const char *httpData) {
    const char *ok200 = "HTTP/1.0 200 OK";
    if (strncmp(httpData, ok200, strlen(ok200)) == 0) {
        char *newlineLoc = strstr(httpData, "\r\n\r\n");
        if (newlineLoc) {
            return newlineLoc + 4; // Skip the newlines.
        }
    }
    return NULL;
}

static char *pathForImage() {
    const char *basePath = "/jobe/vids";
    time_t rawtime = time(NULL);
    struct tm *curtime = localtime(&rawtime);
    const char *dayDirFormat = "%Y%m%d";
    char dayDir[15] = {0};
    (void)strftime(dayDir, sizeof(dayDir), dayDirFormat, curtime);

    const char *hourFormat = "%H";
    char hourDir[4] = {0};
    (void)strftime(hourDir, sizeof(hourDir), hourFormat, curtime);

    const char *fileFormat = "driveway_snap_%Y%m%d_%H%M%S.jpg";
    char file[40] = {0};
    (void)strftime(file, sizeof(file), fileFormat, curtime);

    char *fullPath = calloc(sizeof(char), 75);
    sprintf(fullPath, "%s/%s", basePath, dayDir);
    mkdir(fullPath, 0755);
    sprintf(fullPath, "%s/%s/%s", basePath, dayDir, hourDir);
    mkdir(fullPath, 0755);
    sprintf(fullPath, "%s/%s/%s/%s", basePath, dayDir, hourDir, file);
    return fullPath;
}

static double difference_of_times(struct timeval *time2, struct timeval *time1) {
    struct timeval resultval;
    resultval.tv_sec = time2->tv_sec;
    resultval.tv_usec = time2->tv_usec;
    resultval.tv_sec -= time1->tv_sec;
    resultval.tv_usec -= time1->tv_usec;

    double diff = (double)resultval.tv_sec;
    diff += (double)resultval.tv_usec / 1000000.0;

    return diff;
}

static void check_camera(const char *ip, struct timeval *last_capture) {
    char *foo = check_for_motion(ip);
    if (foo) {
        char *data = startOfDataForHTTPRequest(foo);
        if (data) {
            char *instr = strstr(data, "mdresult=");
            if (instr != NULL) {
                const char *noMotionString = "mdresult=no motion";
                if (strncmp(instr, noMotionString, strlen(noMotionString)) != 0) {
                    syslog(LOG_WARNING, "[%s] Motion detected\n", ip);

                    struct timeval current_capture;
                    gettimeofday(&current_capture, NULL);

                    double diff = difference_of_times(&current_capture, last_capture);

                    if (diff >= 1.0) {
                        size_t len = 0;
                        struct timeval start_capture;
                        gettimeofday(&start_capture, NULL);
                        char *snapshotHTTP = captureSnapshot(ip, &len);
                        struct timeval end_capture;
                        gettimeofday(&end_capture, NULL);
                        if (snapshotHTTP) {
                            char *snapshot = startOfDataForHTTPRequest(snapshotHTTP);
                            syslog(LOG_WARNING, "[%s] Captured image of size %zu bytes in %f\n", ip, len, difference_of_times(&end_capture, &start_capture));
                            char *filePath = pathForImage();
                            syslog(LOG_WARNING, "[%s] Saving to path %s\n", ip, filePath);
                            FILE *file = fopen(filePath, "w");
                            free(filePath);
                            fwrite(snapshot, sizeof(char), len, file);
                            fclose(file);
                            free(snapshotHTTP);
                            gettimeofday(last_capture, NULL);
                        } else {
                            syslog(LOG_WARNING, "[%s] Error capturing image.\n", ip);
                        }
                    }
                    //      break;
                }
            }
        }
        free(foo);
    }
}

void *camera_loop(void *ipPointer) {
    const char *ip = (const char *)ipPointer;
    struct timeval last_capture;
    gettimeofday(&last_capture, NULL);

    while(1) {
        check_camera(ip, &last_capture);
        usleep(50000);
    }
}

int main(int argc, const char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <camera ip> ...\n", argv[0]);
        return 1;
    }

    openlog(NULL, LOG_PID | LOG_CONS, LOG_DAEMON);
  
    syslog(LOG_WARNING, "%s starting up...\n", argv[0]);

    int i;
    for (i = 1; i < argc; i++) {
        pthread_t thread;
        int rc = pthread_create(&thread, NULL, camera_loop, (void *)argv[i]);
        if (rc) {
            fprintf(stderr, "Error creating thread: %d\n", rc);
            return -1;
        }
    }

    while (1) {
        sleep(15);
        syslog(LOG_WARNING, "Still alive.\n");
    }
}
