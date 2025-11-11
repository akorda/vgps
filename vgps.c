#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int running = 1;
int verbose = 0;

void
handle_sigint(int sig)
{
    if (verbose) {
        printf("Ctr+C detected. Exit\n");
    }
    running = 0;
}

int
error_close_master(int mfd)
{
    int err = errno;
    close(mfd); /* Might change 'errno' */
    errno = err;
    return EXIT_FAILURE;
}

unsigned char
nmea_checksum(const char* sentence)
{
    unsigned char checksum = 0;
    int start = 0;

    /* Skip leading '$' if present */
    if (sentence[0] == '$') {
        start = 1;
    }

    /* XOR all characters until '*' or end of string */
    for (int i = start; sentence[i] != '\0' && sentence[i] != '*'; i++) {
        checksum ^= (unsigned char)sentence[i];
    }

    return checksum;
}

void
write_nmea_messages(int mfd, double latitude, double longitude, double elevation)
{
    // NMEA minimal sequence:
    // $GPGGA,231531.521,5213.788,N,02100.712,E,1,12,1.0,0.0,M,0.0,M,,*6A
    // $GPGSA,A,1,,,,,,,,,,,,,1.0,1.0,1.0*30
    // $GPRMC,231531.521,A,5213.788,N,02100.712,E,,,261119,000.0,W*72

    char date_now[7], time_now[7];
    char NS, WE;
    int lat_deg, lon_deg;
    double lat_min, lon_min;
    char lat_text[20], lon_text[20];
    char nmea[255], gpgga[255], gpgsa[255], gprmc[255];

    /* N or S */
    if (latitude > 0) {
        NS = 'N';
    } else {
        NS = 'S';
    }

    /* W or E */
    if (longitude > 0) {
        WE = 'E';
    } else {
        WE = 'W';
    }

    latitude = fabs(latitude);
    longitude = fabs(longitude);
    lat_deg = (int)latitude;
    lon_deg = (int)longitude;
    lat_min = (latitude - lat_deg) * 60;
    lon_min = (longitude - lon_deg) * 60;
    sprintf(lat_text, "%02d%07.4f", lat_deg, lat_min);
    sprintf(lon_text, "%03d%07.4f", lon_deg, lon_min);

    time_t nowt = time(NULL);
    const struct tm* now = localtime(&nowt);
    sprintf(date_now, "%02d%02d%02d", now->tm_mday, now->tm_mon, (now->tm_year + 1900) % 100);
    sprintf(time_now, "%02d%02d%02d", now->tm_hour, now->tm_min, now->tm_sec);

    sprintf(gpgsa, "GPGGA,%s,%s,%d,%s,%d,1,12,1.0,%s,M,0.0,M,,", time_now, lat_text, NS, lon_text, WE, elevation);
    sprintf(nmea, "$%s*%d\n", gpgsa, nmea_checksum(gpgsa));
    write(mfd, nmea, strlen(nmea));

    sprintf(gpgga, "GPGSA,A,3,,,,,,,,,,,,,1.0,1.0,1.0");
    sprintf(nmea, "$%s*%d\n", gpgga, nmea_checksum(gpgga));
    write(mfd, nmea, strlen(nmea));

    sprintf(gprmc, "GPRMC,%s,A,%s,%d,%s,%d,,,%s,000.0,W", time_now, lat_text, NS, lon_text, WE, date_now);
    sprintf(nmea, "$%s*%d\n", gprmc, nmea_checksum(gprmc));
    write(mfd, nmea, strlen(nmea));
}

int
main(int argc, char* argv[])
{
    int opt;
    char pts_name[100];

    while ((opt = getopt(argc, argv, "v")) != -1) {
        switch (opt) {
            case 'v':
                verbose = 1;
                break;
            case '?': /* Unknown option or missing argument */
                fprintf(stderr, "Unknown option: -%c\n", optopt);
                return EXIT_FAILURE;
            default:
                fprintf(stderr, "Unexpected error parsing options.\n");
                return EXIT_FAILURE;
        }
    }

    int mfd = posix_openpt(O_RDWR | O_NOCTTY); /* Open pty master */

    if (mfd == -1) {
        return EXIT_FAILURE;
    }

    if (grantpt(mfd) == -1) /* Grant access to slave pty */
    {
        return error_close_master(mfd);
    }

    if (unlockpt(mfd) == -1) {
        return error_close_master(mfd);
    }

    /* The returned storage is good until the next call of `ptsname` function.
     * Therefore we have to save it to our buffer */
    char* p = ptsname(mfd);
    if (p == NULL) {
        return error_close_master(mfd);
    }

    strncpy(pts_name, p, sizeof(pts_name));
    pts_name[sizeof(pts_name) - 1] = '\0';
    if (verbose) {
        printf("Slave name is: %s\n", pts_name);
    } else {
        printf("%s\n", pts_name);
    }

    /* Register the signal handler */
    if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        return error_close_master(mfd);
    }

    double lat = 23.7401864, lon = 38.3178297, elevation = 0;

    /* keep generating NMEA messages */
    while (running) {
        write_nmea_messages(mfd, lat, lon, elevation);
        sleep(1);
    }

    /* cleanup the master */
    close(mfd);

    return EXIT_SUCCESS;
}
