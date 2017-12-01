#include <stdint.h>
#include <limits.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <time.h>               /* clock_t, clock, CLOCKS_PER_SEC */
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#ifdef THREADS
#include <pthread.h>
#endif

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

#ifdef THREADS
struct thread_info {            /* Used as argument to thread_start() */
    pthread_t thread_id;        /* ID returned by pthread_create() */
    uint64_t thread_num;        /* Application-defined thread # */
    geopoint_t *hotels;
    uint64_t n_hotels;
    geopoint_t *landmarks;
    uint64_t n_landmarks;
    const char *type_hotels;
    double t0;
    uint64_t count;
    uint64_t swapped;
};

#define handle_error_en(en, msg) \
    do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

#ifndef NUM_THREADS
#define NUM_THREADS 4
#endif

#define INCR(v) __atomic_add_fetch(&v,1,__ATOMIC_SEQ_CST)
#define INTERSECT partition_intersect_hotels
#define _THREADS 1
#else                           /* !THREADS */
#define INTERSECT intersect_hotels
#define INCR(v) v++
#define _THREADS 0
#endif                          /* THREADS */

double dtime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0f);
}

geopoint_t *binsearch_start(geopoint_t * key, geopoint_t * points, uint64_t n)
{
    uint64_t l = 0;
    uint64_t h = n;             // Not n - 1
    while (l < h) {
        uint64_t mid = (l + h) / 2;
        if (key->latitude - 50.0 <= points[mid].latitude) {
            h = mid;
        } else {
            l = mid + 1;
        }
    }
    return points + l;
}

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

#define SECS(n) ((double)(n))
static inline geopoint_t *read_geopoints(char *filename, uint64_t * count, char *type)
{
    uint64_t n_points = 0;
    uint64_t s_points = 1000000;
    geopoint_t *points = malloc(sizeof(geopoint_t) * s_points);
    geopoint_t *point = points;
    FILE *f_points;
    char line[256];
    double read_secs, sort_secs;
    double t0, t1;

    t0 = dtime();

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
    t1 = dtime();
    read_secs = SECS(t1 - t0);

    qsort(points, n_points, sizeof(geopoint_t), cmp_geopoint);
    t0 = dtime();
    sort_secs = SECS(t0 - t1);

    printf("Loaded %lu %s from '%s', read took %.2lfsecs, sort took %.2lfsecs\n",
           n_points, type, filename, read_secs, sort_secs);

    *count = n_points;
    return points;
}

static inline geopoint_t *scan_landmarks(geopoint_t * hotel, geopoint_t * lmw_start, const geopoint_t * landmarks_end,
                                         uint64_t swapped)
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
                                    (swapped ? hotel->km_long_mul : landmark->km_long_mul));

            if (UNLIKELY(long_dist < 50.0)) {
                double lat_dist_sq = SQR(lat_dist);
                double long_dist_sq = SQR(long_dist);
                double dist_sq = long_dist_sq + lat_dist_sq;

                if (UNLIKELY(dist_sq <= D0)) {
                    if (0 && hotel->id == 23805 /*&& landmark->id == 900123653 */ ) {
                        /* 5.927530273 */
                        printf("# hotel %lu distance to L %lu: %.10f H(%lf,%lf) - L(%lf,%lf) (%lu)\n",
                               hotel->id, landmark->id, sqrt(dist_sq), hotel->latitude, hotel->longitude,
                               landmark->latitude, landmark->longitude, swapped);
                    }
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
                                        const uint64_t swapped, const char *type_hotels, double t0)
{
    const geopoint_t *hotels_end = hotels + n_hotels;
    const geopoint_t *landmarks_end = landmarks + n_landmarks;
    geopoint_t *lmw_start = _THREADS ? binsearch_start(hotels, landmarks, n_landmarks) : landmarks;
    geopoint_t *hotel;
    uint64_t count = 0;
    double last_elapsed = 0.0f;

    for (hotel = hotels; hotel < hotels_end; hotel++) {
        lmw_start = scan_landmarks(hotel, lmw_start, landmarks_end, swapped);
        if (++count % 100 == 0) {
            const double t1 = dtime();
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

void print_results(const char *outname, geopoint_t * const landmarks, const uint64_t n_landmarks)
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

#ifdef THREADS
static void *thread_start(void *arg)
{
    struct thread_info *tinfo = arg;
    double t0 = dtime();
    double t1;

    printf("thread start thread %lu; count: %lu hotels\n", tinfo->thread_num, tinfo->n_hotels);
    tinfo->count =
        intersect_hotels(tinfo->hotels, tinfo->n_hotels, tinfo->landmarks, tinfo->n_landmarks, tinfo->swapped,
                         tinfo->type_hotels, t0);
    t1 = dtime();
    printf("Processed %.2f%% (%lu) of %s in %.2lfsecs @ %.2lf/sec\n",
           (double)tinfo->count / (double)tinfo->n_hotels * 100.0, tinfo->count, tinfo->type_hotels, SECS(t1 - t0),
           tinfo->count / SECS(t1 - t0));
    fflush(stdout);

    return &tinfo->count;
}

inline static uint64_t partition_intersect_hotels(geopoint_t * const hotels, const uint64_t n_hotels,
                                                  geopoint_t * const landmarks, const uint64_t n_landmarks,
                                                  const uint64_t swapped, const char *type_hotels, double t0)
{
    struct thread_info tinfo[NUM_THREADS];
    uint64_t incr = (n_hotels + NUM_THREADS - 1) / NUM_THREADS;
    uint64_t i;
    uint64_t count = 0;
    int s;
    pthread_attr_t attr;

    s = pthread_attr_init(&attr);
    if (s != 0)
        handle_error_en(s, "pthread_attr_init");

    for (i = 0; i < NUM_THREADS; i++) {
        uint64_t start = (incr * i);
        uint64_t end = start + incr;
        if (end > n_hotels)
            end = n_hotels;

        tinfo[i].thread_num = i;
        tinfo[i].hotels = hotels + start;
        tinfo[i].n_hotels = end - start;
        tinfo[i].landmarks = landmarks;
        tinfo[i].n_landmarks = n_landmarks;
        tinfo[i].swapped = swapped;
        tinfo[i].type_hotels = type_hotels;
        tinfo[i].t0 = t0;

        s = pthread_create(&tinfo[i].thread_id, &attr, &thread_start, &tinfo[i]);
        if (s != 0)
            handle_error_en(s, "pthread_create");

    }

    s = pthread_attr_destroy(&attr);
    if (s != 0)
        handle_error_en(s, "pthread_attr_destroy");

    /* Now join with each thread, and display its returned value */

    for (i = 0; i < NUM_THREADS; i++) {
        void *res;
        uint64_t c;
        s = pthread_join(tinfo[i].thread_id, &res);
        if (s != 0)
            handle_error_en(s, "pthread_join");
        c = *((uint64_t *) res);

        printf("Joined with thread %lu; returned value was %lu\n", tinfo[i].thread_num, c);
        count += c;
    }
    return count;
}
#endif

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
    double t0 = dtime();
    double start_time = t0;
    double t1;
    uint64_t swapped = 0;
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

    count = INTERSECT(hotels, n_hotels, landmarks, n_landmarks, swapped, type_hotels, t0);

    t1 = dtime();
    printf("Processed %.2f%% (%lu) of %s in %.2lfsecs @ %.2lf/sec\n",
           (double)count / (double)n_hotels * 100.0, count, type_hotels, SECS(t1 - t0), count / SECS(t1 - t0));
    fflush(stdout);

    sprintf(outname, "%s.out", name_hotels);
    print_results(outname, hotels, n_hotels);

    t0 = dtime();

    printf("Wrote %lu %s records to %s.out in %.2lfsecs\n", n_hotels, type_hotels, name_hotels, SECS(t0 - t1));

    /* now print the landmark data out */
    sprintf(outname, "%s.out", name_landmarks);

    print_results(outname, landmarks, n_landmarks);

    t1 = dtime();

    printf("Wrote %lu %s records to %s.out in %.2lfsecs\n", n_landmarks, type_landmarks, name_landmarks, SECS(t1 - t0));
    printf("Finished in %.2lfsec\n", SECS(t1 - start_time));
    return 0;
}
