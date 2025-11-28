#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ROUND_TO(n) (floor(1000000*n)/1000000)

const char *CONFIG_FILE = "/etc/vgps.conf";

int running = 1;
int verbose = 0;
double latitude = 0, longitude = 0, elevation = 0;

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
write_nmea_messages(int mfd)
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
    char nmea[82], gga[82], gsa[82], rmc[82];

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
    const struct tm* utc_now = gmtime(&nowt);
    sprintf(date_now, "%02d%02d%02d", utc_now->tm_mday, utc_now->tm_mon, (utc_now->tm_year + 1900) % 100);
    sprintf(time_now, "%02d%02d%02d", utc_now->tm_hour, utc_now->tm_min, utc_now->tm_sec);

    sprintf(gga, "GPGGA,%s,%s,%d,%s,%d,1,12,1.0,%f.2,M,0.0,M,,", time_now, lat_text, NS, lon_text, WE, elevation);
    sprintf(nmea, "$%s*%02X\r\n", gga, nmea_checksum(gga));
    write(mfd, nmea, strlen(nmea));

    sprintf(gsa, "GPGSA,A,3,,,,,,,,,,,,,1.0,1.0,1.0");
    sprintf(nmea, "$%s*%02X\r\n", gsa, nmea_checksum(gsa));
    write(mfd, nmea, strlen(nmea));

    sprintf(rmc, "GPRMC,%s,A,%s,%d,%s,%d,,,%s,000.0,W", time_now, lat_text, NS, lon_text, WE, date_now);
    sprintf(nmea, "$%s*%02X\r\n", rmc, nmea_checksum(rmc));
    write(mfd, nmea, strlen(nmea));
}

int
parse_args(int argc, char* argv[])
{
    int opt;

    while ((opt = getopt(argc, argv, "vt:n:e:")) != -1) {
        switch (opt) {
            case 't':
                latitude = atof(optarg);
                break;
            case 'n':
                longitude = atof(optarg);
                break;
            case 'e':
                elevation = atof(optarg);
                break;
            case 'v':
                verbose = 1;
                break;
            case '?': /* Unknown option or missing argument */
                if (optopt == 't' || optopt == 'n' || optopt == 'e') {
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                } else {
                    fprintf(stderr, "Unknown option: -%c\n", optopt);
                }
                return -1;
            default:
                fprintf(stderr, "Unexpected error parsing options.\n");
                return -1;
        }
    }

    return 0;
}

int
read_config_file() {
    if (access(CONFIG_FILE, R_OK) != 0) {
        return EXIT_SUCCESS;
    }

    FILE* file = fopen(CONFIG_FILE, "r");
    if (file == NULL) {
      return EXIT_FAILURE;
    }

    const int MAX_LINE_LEN = 120;
    const char delimeter[] = "=";

    char line[MAX_LINE_LEN];
    while (fgets(line, MAX_LINE_LEN, file))
    {
        /* If the line is empty or starts with # (a comment) ignore it */
        if (strlen(line) == 0 || line[0] == '#') {
            continue;
        }

        char *token = strtok(line, delimeter);
        if (token == NULL) {
            continue;
        }
        
        if (strcmp(token, "latitude") == 0) {
            token = strtok(NULL, delimeter);
            if (token != NULL) {
                latitude = ROUND_TO(atof(token));
            }
        } else if (strcmp(token, "longitude") == 0) {
            token = strtok(NULL, delimeter);
            if (token != NULL) {
                longitude = ROUND_TO(atof(token));
            }
        } else if (strcmp(token, "elevation") == 0) {
            token = strtok(NULL, delimeter);
            if (token != NULL) {
                elevation = ROUND_TO(atof(token));
            }
        }
    }

    fclose(file);
    return EXIT_SUCCESS;
}

int
main(int argc, char* argv[])
{
    if (read_config_file() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    char pts_name[100];

    if (parse_args(argc, argv) != 0) {
        return EXIT_FAILURE;
    }

    int mfd = posix_openpt(O_RDWR | O_NOCTTY); /* Open pty master */

    if (mfd == -1) {
        return EXIT_FAILURE;
    }

    /* Grant access to slave pty */
    if (grantpt(mfd) == -1) {
        fprintf(stderr, "Error: Unable to grant access to slave pty: %s\n", strerror(errno));
        return error_close_master(mfd);
    }

    if (unlockpt(mfd) == -1) {
        fprintf(stderr, "Error: Unable to unlock: %s\n", strerror(errno));
        return error_close_master(mfd);
    }

    /* The returned storage is good until the next call of `ptsname` function.
     * Therefore we have to save it to our buffer */
    char* name = ptsname(mfd);
    if (name == NULL) {
        fprintf(stderr, "Error: Unable get the slave pty name: %s\n", strerror(errno));
        return error_close_master(mfd);
    }

    strncpy(pts_name, name, sizeof(pts_name));
    pts_name[sizeof(pts_name) - 1] = '\0';
    printf("%s\n", pts_name);

    /* Set permissions to 0444 (octal) -> read-only for all */
    if (chmod(pts_name, 0444) != 0) {
        fprintf(stderr, "Error: Unable to change permissions of '%s': %s\n", pts_name, strerror(errno));
        return error_close_master(mfd);
    }

    /* Register the signal handler */
    if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        fprintf(stderr, "Error: Unable to handle the SIGINT signal: %s\n", strerror(errno));
        return error_close_master(mfd);
    }

    /* keep generating NMEA messages */
    while (running) {
        write_nmea_messages(mfd);
        sleep(1);
    }

    /* cleanup the master */
    close(mfd);

    return EXIT_SUCCESS;
}
