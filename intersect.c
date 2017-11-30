#include <stdint.h>
#include <limits.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <time.h>               /* clock_t, clock, CLOCKS_PER_SEC */

typedef struct geopoint {
    double latitude;
    double longitude;
    double km_long_mul;
    double km_to_meridian;
    double km_to_equator;
    uint64_t id;
    uint64_t dist[6];           /* 50, 25, 10, 5, 2, 1 KM */
} geopoint_t;

#ifndef USE_LIKELY
#define USE_LIKELY 1
#endif

#if USE_LIKELY
#define LIKELY(n)   __builtin_expect((n),1)
#define UNLIKELY(n) __builtin_expect((n),0)
#else
#define LIKELY(n) (n)
#define UNLIKELY(n) (n)
#endif

#define CMP(a,b) ( ((a) < (b)) ? -1 : ((a) == (b)) ? 0 : 1 )
#define deg2rad(deg) (deg * M_PI / 180.0f)

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288f
#endif

#define KM_LAT       111.325f   /* Taken from Bookings::Geo::Point */
#define KM_LONG_MUL  111.12f    /* Taken from Bookings::Geo::Point */

#define SQR(n) (n * n)

#define D0 SQR(50.0f)
#define D1 SQR(25.0f)
#define D2 SQR(10.0f)
#define D3 SQR( 5.0f)
#define D4 SQR( 2.0f)
#define D5 SQR( 1.0f)

#define INCR(v) v++

int cmp_geopoint(const void *va, const void *vb)
{
    geopoint_t *a = (geopoint_t *) va;
    geopoint_t *b = (geopoint_t *) vb;
    int c = CMP(a->latitude, b->latitude);
    if (c)
        return c;
    c = CMP(a->longitude, b->longitude);
    if (c)
        return c;
    c = CMP(a->id, b->id);
    return c;
}

#define SECS(n) ((double)(n) / (double)CLOCKS_PER_SEC)
static inline geopoint_t *read_geopoints(char *filename, uint64_t * count, char *type)
{
    uint64_t n_points = 0;
    uint64_t s_points = 1000000;
    geopoint_t *points = malloc(sizeof(geopoint_t) * s_points);
    geopoint_t *point = points;
    FILE *f_points;
    char line[256];
    double read_secs, sort_secs;
    clock_t t0, t1;

    t0 = clock();

    f_points = fopen(filename, "r");
    assert(points);
    assert(f_points);

    if (!fgets(line, sizeof(line), f_points)) {
        *count = 0;
        return NULL;
    }

    while (3 == fscanf(f_points, "%lu\t%lf\t%lf\n", &point->id, &point->latitude, &point->longitude)) {
        point->km_long_mul = (KM_LONG_MUL * cos(deg2rad(point->latitude)));
        point->km_to_meridian = point->longitude * point->km_long_mul;
        point->km_to_equator = point->latitude * KM_LAT;
        bzero(point->dist, sizeof(uint64_t) * 6);
        n_points++;
        if (n_points >= s_points) {
            s_points += 1000000;
            points = realloc(points, sizeof(geopoint_t) * s_points);
            assert(points);
            point = points + n_points;
        } else {
            point++;
        }
    }
    fclose(f_points);
    t1 = clock();
    read_secs = SECS(t1 - t0);

    qsort(points, n_points, sizeof(geopoint_t), cmp_geopoint);
    t0 = clock();
    sort_secs = SECS(t0 - t1);

    printf("Loaded %lu %s from '%s', read took %.2lfsecs, sort took %.2lfsecs\n",
           n_points, type, filename, read_secs, sort_secs);

    *count = n_points;
    return points;
}

static inline geopoint_t *scan_landmarks(geopoint_t * hotel, geopoint_t * lmw_start, const geopoint_t * landmarks_end,
                                         int swapped)
{
    geopoint_t *landmark;
    for (landmark = lmw_start; landmark < landmarks_end; landmark++) {
        double lat_dist = landmark->km_to_equator - hotel->km_to_equator;

        if (UNLIKELY(lat_dist < -50.0)) {
            lmw_start = landmark + 1;
            continue;
        } else if (UNLIKELY(lat_dist > 50.0)) {
            break;
        } else {
            double long_dist = fabs((landmark->longitude - hotel->longitude) *
                                    (swapped ? landmark->km_long_mul : hotel->km_long_mul));

            if (UNLIKELY(long_dist < 50.0)) {
                double lat_dist_sq = SQR(lat_dist);
                double long_dist_sq = SQR(long_dist);
                double dist_sq = long_dist_sq + lat_dist_sq;

                if (UNLIKELY(dist_sq <= D0)) {
                    INCR(hotel->dist[0]);
                    INCR(landmark->dist[0]);
                    if (UNLIKELY(dist_sq <= D1)) {
                        INCR(landmark->dist[1]);
                        INCR(hotel->dist[1]);
                        if (UNLIKELY(dist_sq <= D2)) {
                            INCR(landmark->dist[2]);
                            INCR(hotel->dist[2]);
                            if (UNLIKELY(dist_sq <= D3)) {
                                INCR(hotel->dist[3]);
                                INCR(landmark->dist[3]);
                                if (UNLIKELY(dist_sq <= D4)) {
                                    INCR(hotel->dist[4]);
                                    INCR(landmark->dist[4]);
                                    if (UNLIKELY(dist_sq <= D5)) {
                                        INCR(hotel->dist[5]);
                                        INCR(landmark->dist[5]);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return lmw_start;
}

inline static uint64_t intersect_hotels(geopoint_t * const hotels, const uint64_t n_hotels,
                                      geopoint_t * const landmarks, const uint64_t n_landmarks,
                                      const int swapped, FILE * out, const char *type_hotels, clock_t t0)
{
    const geopoint_t *hotels_end = hotels + n_hotels;
    const geopoint_t *landmarks_end = landmarks + n_landmarks;
    geopoint_t *lmw_start = landmarks;
    geopoint_t *hotel;
    uint64_t count = 0;
    double last_elapsed = 0.0f;

    for (hotel = hotels; hotel < hotels_end; hotel++) {
        lmw_start = scan_landmarks(hotel, lmw_start, landmarks_end, swapped);
        fprintf(out, "%lu\t%lf\t%lf\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\n",
                hotel->id, hotel->latitude, hotel->longitude,
                hotel->dist[0], hotel->dist[1], hotel->dist[2], hotel->dist[3], hotel->dist[4], hotel->dist[5]
            );
        if (++count % 100 == 0) {
            const clock_t t1 = clock();
            const double elapsed = SECS(t1 - t0);
            if (elapsed - last_elapsed >= 1.0) {
                printf("Processed %.2f%% (%lu) of %s in %.2lfsecs @ %.2lf/sec\r",
                       (double)count / (double)n_hotels * 100.0, count, type_hotels, SECS(t1 - t0),
                       count / SECS(t1 - t0));
                fflush(stdout);
                last_elapsed = elapsed;
            }
        }
    }
    return count;
}

void print_landmarks(const char *outname, geopoint_t * const landmarks, const uint64_t n_landmarks)
{
    const geopoint_t *landmarks_end = landmarks + n_landmarks;
    FILE *out = fopen(outname, "w");
    geopoint_t *landmark;

    for (landmark = landmarks; landmark < landmarks_end; landmark++) {
        fprintf(out, "%lu\t%lf\t%lf\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\n",
                landmark->id, landmark->latitude, landmark->longitude,
                landmark->dist[0], landmark->dist[1], landmark->dist[2], landmark->dist[3],
                landmark->dist[4], landmark->dist[5]
            );
    }
    fclose(out);
}

int main(int argc, char **argv)
{
    uint64_t n_hotels = 0;
    uint64_t n_landmarks = 0;
    uint64_t n_tmp;
    geopoint_t *tmp;
    geopoint_t *hotels;
    geopoint_t *landmarks;
    char *name_hotels;
    char *name_landmarks;
    char *name_tmp;
    char *type_hotels = "hotels";
    char *type_landmarks = "landmarks";
    uint64_t count = 0;
    clock_t t0 = clock();
    clock_t t1;
    int swapped = 0;
    char outname[1024];

    if (argc < 3) {
        printf("intersect H L\n");
        exit(0);
    }

    name_hotels = argv[1];
    hotels = read_geopoints(name_hotels, &n_hotels, type_hotels);

    name_landmarks = argv[2];
    landmarks = read_geopoints(name_landmarks, &n_landmarks, type_landmarks);

    if (n_hotels < n_landmarks) {
        tmp = hotels;
        hotels = landmarks;
        landmarks = tmp;

        n_tmp = n_hotels;
        n_hotels = n_landmarks;
        n_landmarks = n_tmp;

        name_tmp = name_hotels;
        name_hotels = name_landmarks;
        name_landmarks = name_tmp;

        name_tmp = type_hotels;
        type_hotels = type_landmarks;
        type_landmarks = name_tmp;

        swapped = 1;
    }

    assert(hotels);
    assert(landmarks);
    sprintf(outname, "%s.out", name_hotels);

    {
        FILE *out;
        out = fopen(outname, "w");
        count = intersect_hotels(hotels, n_hotels, landmarks, n_landmarks, swapped, out, type_hotels, t0);
        fclose(out);
    }

    t1 = clock();
    printf("Processed %.2f%% (%lu) of %s in %.2lfsecs @ %.2lf/sec\n",
           (double)count / (double)n_hotels * 100.0, count, type_hotels, SECS(t1 - t0), count / SECS(t1 - t0));
    fflush(stdout);

    /* now print the landmark data out */
    sprintf(outname, "%s.out", name_landmarks);

    print_landmarks(outname, landmarks, n_landmarks);

    t0 = clock();

    printf("Wrote %lu %s records to %s.out in %.2lfsecs\n", n_landmarks, type_landmarks, name_landmarks, SECS(t0 - t1));
    return 0;
}
