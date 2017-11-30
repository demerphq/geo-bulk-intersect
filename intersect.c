#include <stdint.h>
#include <limits.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

typedef struct geopoint {
    double latitude;
    double longitude;
    double km_to_meridian;
    double km_to_equator;
    uint64_t id;
    uint64_t dist[6]; /* 50, 25, 10, 5, 2, 1 KM */ 
} geopoint_t;

#define CMP(a,b) ( ((a) < (b)) ? -1 : ((a) == (b)) ? 0 : 1 )
#define deg2rad(deg) (deg * M_PI / 180.0)

#define KM_LAT       111.325    /* Taken from Bookings::Geo::Point */
#define KM_LONG_MUL  111.12     /* Taken from Bookings::Geo::Point */

#define D0 (50.0 * 50.0)
#define D1 (25.0 * 25.0)
#define D2 (10.0 * 10.0)
#define D3 ( 5.0 *  5.0)
#define D4 ( 2.0 *  2.0)
#define D5 ( 1.0 *  1.0)



int cmp_geopoint(const void *va, const void *vb) {
    geopoint_t *a= (geopoint_t *)va;
    geopoint_t *b= (geopoint_t *)vb;
    int c= CMP(a->latitude,b->latitude);
    if (c) return c;
    c= CMP(a->longitude,b->longitude);
    if (c) return c;
    c= CMP(a->id,b->id);
    return c;
}

geopoint_t * read_geopoints(char *filename, uint64_t *count)
{
    uint64_t n_points= 0;
    uint64_t s_points= 1000000;
    geopoint_t *points= malloc(sizeof(geopoint_t) * s_points);
    geopoint_t *point= points;
    FILE *f_points = fopen(filename,"r");
    char line[256];
    
    assert(points);

    if (!fgets(line, sizeof(line), f_points)) {
        *count = 0;
        return NULL;
    }

    while (3 == fscanf(f_points,"%lu\t%lf\t%lf\n",&point->id,&point->latitude,&point->longitude)) {
        point->km_to_meridian= point->longitude * (KM_LONG_MUL * cos(deg2rad(point->latitude)));
        point->km_to_equator= point->latitude * KM_LAT;
        bzero(point->dist,sizeof(uint64_t)*6);
        n_points++;
        if (n_points >= s_points) {
            s_points += 1000000;
            points= realloc(points, sizeof(geopoint_t) * s_points);
            assert(points);
            point= points + n_points;
        } else {
            point++;
        }
    }
    fclose(f_points);
    

    qsort(points, n_points, sizeof(geopoint_t), cmp_geopoint);

    printf("Finished reading and sorting %lu points from '%s'\n", n_points, filename);

    *count= n_points;
    return points;
}


int main(int argc, char **argv) {
    uint64_t n_hotels= 0;
    uint64_t n_landmarks= 0;
    geopoint_t *hotel;
    geopoint_t *hotels;
    geopoint_t *hotels_end;
    geopoint_t *lmw_start;
    geopoint_t *landmark;
    geopoint_t *landmarks;
    geopoint_t *landmarks_end;
    uint64_t count= 0;
    char outname[1024];
    FILE *out;

    if (argc<3) { 
        printf("intersect H L\n");
        exit(0);
    }

    hotels= read_geopoints(argv[1], &n_hotels);
    hotels_end= hotels + n_hotels;

    lmw_start= landmarks= read_geopoints(argv[2], &n_landmarks);
    landmarks_end= landmarks + n_landmarks;
    
    assert(hotels);
    assert(landmarks);
    sprintf(outname, "%s.out", argv[1]);
    out= fopen(outname, "w");

    for (hotel= hotels; hotel < hotels_end; hotel++) {
        for ( landmark= lmw_start; landmark < landmarks_end; landmark++ ) {
            double lat_dist= landmark->km_to_equator - hotel->km_to_equator;
            
            if (lat_dist < -50.0) {
                lmw_start= landmark + 1;
                continue;
            } else if (lat_dist > 50.0) {
                break;
            } else {
                double long_dist= fabs(landmark->km_to_meridian - hotel->km_to_meridian);

                if (long_dist < 50.0) {
                    double lat_dist_sq= pow(lat_dist, 2.0);
                    double long_dist_sq= pow(long_dist, 2.0);
                    double dist_sq = long_dist_sq + lat_dist_sq;

                    if (dist_sq <= D0) {
                        hotel->dist[0]++;
                        landmark->dist[0]++;
                        if (dist_sq <= D1) {
                            landmark->dist[1]++;
                            hotel->dist[1]++;
                            if (dist_sq <= D2) {
                                landmark->dist[2]++;
                                hotel->dist[2]++;
                                if (dist_sq <= D3) {
                                    hotel->dist[3]++;
                                    landmark->dist[3]++;
                                    if (dist_sq <= D4) {
                                        hotel->dist[4]++;
                                        landmark->dist[4]++;
                                        if (dist_sq <= D5) {
                                            hotel->dist[5]++;
                                            landmark->dist[5]++;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            } 
        }
        fprintf(out,"%lu\t%lf\t%lf\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\n",
                hotel->id, 
                hotel->latitude,
                hotel->longitude,
                hotel->dist[0],
                hotel->dist[1],
                hotel->dist[2],
                hotel->dist[3],
                hotel->dist[4],
                hotel->dist[5]
        );
    }
    fclose(out);
    sprintf(outname, "%s.out", argv[2]);
    out= fopen(outname, "w");
    for ( landmark= landmarks; landmark < landmarks_end; landmark++ ) {
        fprintf(out, "%lu\t%lf\t%lf\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\n",
                landmark->id, 
                landmark->latitude,
                landmark->longitude,
                landmark->dist[0],
                landmark->dist[1],
                landmark->dist[2],
                landmark->dist[3],
                landmark->dist[4],
                landmark->dist[5]
        );
    }
    fclose(out);
    return 0;
}
