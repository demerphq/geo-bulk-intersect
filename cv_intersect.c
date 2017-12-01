/*
clang -std=c99 -g -Wall -Wextra -pedantic -Wpadded -Wno-gnu-empty-initializer -O3 -DNDEBUG cv_intersect.c -o cv_intersect; and ./cv_intersect
*/
#include <assert.h>
#include <math.h>               /* fabs */
#include <stdio.h>
#include <stdlib.h>             /* exit qsort */
#include <time.h>               /* clock_t, clock, CLOCKS_PER_SEC */
#include <stdint.h>
#include <string.h>

#define internal static

#define u8 uint8_t
#define u32 uint32_t
#define u64 uint64_t
#define f32 float
#define f64 double

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

typedef struct latlong {
    f64 lat;
    f64 lng;
    f64 cos_lat;
    u64 id;
} latlong;

#define earth_radius_km  6371.0f
#define pi  3.14159265358979f
#define km_per_lat  111.325f

inline f64 km_per_lng(latlong ll)
{
    f64 kml = 111.12f * ll.cos_lat;
    return kml;
}

// can be optimized by not doing sqrt and replacing pow() with mul
inline f64 calculate_distance2(latlong from, latlong to)
{
    f64 d = 0;

    f64 km_lng = km_per_lng(from);      // does it matter if from or to??? A little!

    f64 diff_lng = (from.lng - to.lng) * km_lng;
    f64 diff_lat = (from.lat - to.lat) * km_per_lat;

    d = diff_lng * diff_lng + diff_lat * diff_lat;

    return d;
}

int compare_lat(const void *a, const void *b)
{
    f32 diff = ((latlong *) a)->lat - ((latlong *) b)->lat;
    return diff == 0 ? 0 : (diff < 0 ? -1 : 1);
}

#define BINSEARCH_ERROR 0
#define BINSEARCH_FOUND 1
#define BINSEARCH_INSERT 2

internal u8 binary_search(latlong * landmarks, u32 num_landmarks, f64 target, u32 * index)
{

    // no data at all
    if (landmarks == NULL) {
        return BINSEARCH_ERROR;
    }
    // empty array, or insert location should be initial element
    if (num_landmarks == 0 || target < landmarks[0].lat) {
        *index = 0;
        return BINSEARCH_INSERT;
    }

    u32 span = num_landmarks;
    u32 mid = num_landmarks / 2;
    u32 large_half;
    while (span > 0) {

        if (target == landmarks[mid].lat) {
            *index = mid;
            return BINSEARCH_FOUND;
        }

        span = span / 2;        // half the range left over
        large_half = span / 2 + (span % 2);     // being clever. But this is ceil

        if (target < landmarks[mid].lat) {
            mid -= large_half;
        } else {
            mid += large_half;
        }

    }

    // target_key is not an element of keys, but we found the closest location
    if (mid == num_landmarks) { // after all other elements
        *index = num_landmarks;
    } else if (target < landmarks[mid].lat) {
        *index = mid;           // displace, shift the rest right
    } else if (target > landmarks[mid].lat) {
        *index = mid + 1;       // not sure if these two are both possible
    } else {
        assert(0);              // cannot happen
    }

    // correctness checks:
    // 1. array has elements, and we should insert at the end, make sure the last element is smaller than the new one
    if (num_landmarks > 0 && *index == num_landmarks) {
        assert(target > landmarks[num_landmarks - 1].lat);
    }
    // 2. array has no elements (we already check this above, but left for completeness)
    if (num_landmarks == 0) {
        assert(*index == 0);
    }
    // 3. array has elements, and we should insert at the beginning
    if (num_landmarks > 0 && *index == 0) {
        assert(target < landmarks[0].lat);      // MUST be smaller, otherwise it would have been found if equal
    }
    // 4. insert somewhere in the middle
    if (*index > 0 && *index < num_landmarks) {
        assert(target < landmarks[*index].ll.lat);      // insert shifts the rest right, MUST be smaller otherwise it would have been found
        assert(landmarks[*index - 1].ll.lat < target);  // element to the left is smaller
    }

    return BINSEARCH_INSERT;
}

void read_csv(char *filename, latlong * items, u32 max_items, u32 * items_count)
{
    FILE *data;
    data = fopen(filename, "r");
    // skip the first line, it has a mysql header thing
    char line[256];
    if (!fgets(line, sizeof(line), data)) {
        *items_count = 0;
        return;
    }

    u32 scan_count;
    u32 items_read = 0;
    while (1) {
        if (items_read >= max_items) {
            printf("insufficient space: %u : %u\n", items_read, max_items);
            exit(1);
        };
        scan_count =
            fscanf(data, "%lu\t%lf\t%lf\n", &(items[items_read].id), &(items[items_read].lat),
                   &(items[items_read].lng));
        if (scan_count != 3 || feof(data)) {
            printf("EOF reached, read %u items from %s\n", items_read, filename);
            break;
        } else {
            items[items_read].cos_lat = cos((pi / 180.0f) * items[items_read].lat);
            items_read++;
        }
    }

    fclose(data);

    *items_count = items_read;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("cv_intersect H L\n");
        exit(0);
    }
    clock_t start = clock();

    printf("Reading Hotels from %s\n", argv[1]);

    u32 max_hotels = 10 * 1000 * 1000;
    latlong *hotels = (latlong *) malloc(max_hotels * sizeof(latlong));

    u32 hotel_count;
    read_csv(argv[1], hotels, max_hotels, &hotel_count);

    printf("Reading Landmarks from %s\n", argv[2]);

    u32 max_landmarks = 25 * 1000 * 1000;
    latlong *landmarks_by_lat = (latlong *) malloc(max_landmarks * sizeof(latlong));

    u32 landmark_count;
    read_csv(argv[2], landmarks_by_lat, max_landmarks, &landmark_count);
    landmarks_by_lat[landmark_count].lat = 5000;        // guard latlong that is bigger than aby real ones
    landmark_count++;

    printf("Time spent reading data %fs\n", (f32) (clock() - start) / (f32) CLOCKS_PER_SEC);

    // read id, long, lat
    // make 1 list:
    // latlong sorted by lat

    // then read the others (id, log, lat)
    // find any item in range of the thing in lat, traverse left and right to find all in range
    // and test ones that are also in range of long, for those do distance calc

    // dump results in some format

    start = clock();
    qsort(hotels, hotel_count, sizeof(latlong), compare_lat);
    printf("Time spent sorting hotels %fs\n", (f32) (clock() - start) / (f32) CLOCKS_PER_SEC);
    qsort(landmarks_by_lat, landmark_count, sizeof(latlong), compare_lat);
    printf("Time spent sorting landmarks %fs\n", (f32) (clock() - start) / (f32) CLOCKS_PER_SEC);

    start = clock();

    u32 landmark_index = 0;
    f32 distances_kmsq[] = { 50.0f * 50.0f, 25.0f * 25.0f, 10.0f * 10.0f, 5.0f * 5.0f, 2.0f * 2.0f, 1.0f * 1.0f };
    f64 max_dist_lat = 50.0f / 111.325f;

    FILE *out = fopen("landmarks_in_range.csv", "w");
    u32 landmarks_in_distance[ARRAY_SIZE(distances_kmsq)];

    u32 i;
    for (i = 0; i < hotel_count; i++) {

        memset(landmarks_in_distance, 0, ARRAY_SIZE(distances_kmsq) * sizeof(u32));
        if (i % 10000 == 0) {
            f32 elapsed = ((f32) (clock() - start) / (f32) CLOCKS_PER_SEC);
            printf("Processed %.3f%% (%u) of hotels in %.2fsecs @ %.0f hotels/sec\r",
                   100.0f * (f32) i / (f32) hotel_count, i, elapsed, (f32) i / elapsed);
            fflush(stdout);
        }

        f32 start_lat = hotels[i].lat - max_dist_lat;

        binary_search(landmarks_by_lat, landmark_count, start_lat, &landmark_index);    // landmark_index is the index where inserting this lat would keep the array sorted
        // printf ("Closest to H[%.2f, %.2f] = L[%.2f, %.2f] (%u)\n", hotels[i].lat, hotels[i].lng, landmarks_by_lat[landmark_index].lat, landmarks_by_lat[landmark_index].lng, landmark_index);

        u32 up = landmark_index;
        // printf("Checking increasing distances in rad from %lu to %lu (lat dist: %f)\n", up, landmark_count, max_dist_rad);
        f32 max_dist = hotels[i].lat + max_dist_lat;
        while (landmarks_by_lat[up].lat < max_dist) {
            f32 distance = calculate_distance2(landmarks_by_lat[up], hotels[i]);

            for (u32 d = 0; d < ARRAY_SIZE(distances_kmsq); d++) {      // just rad

                if (distance <= (distances_kmsq[d])) {
                    landmarks_in_distance[d]++;
                } else {
                    break;
                }
            }
            up++;
        }

        fprintf(out, "%lu\t%lf\t%lf\t\t%u\t%u\t%u\t%u\t%u\t%u\n", hotels[i].id, hotels[i].lat, hotels[i].lng,
                landmarks_in_distance[0], landmarks_in_distance[1], landmarks_in_distance[2], landmarks_in_distance[3],
                landmarks_in_distance[4], landmarks_in_distance[5]
            );
    }

    fclose(out);
    {
        f32 elapsed = ((f32) (clock() - start) / (f32) CLOCKS_PER_SEC);
        printf("Processed %.3f%% (%u) of hotels in %.2fsecs @ %.0f hotels/sec\n",
               100.0f * (f32) i / (f32) hotel_count, hotel_count, elapsed, (f32) i / elapsed);
    }
    printf("\nTime spent distance finding %fs\n", (f32) (clock() - start) / (f32) CLOCKS_PER_SEC);

    exit(0);
}
