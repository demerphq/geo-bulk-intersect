/*
clang -std=c99 -g -Wall -Wextra -pedantic -Wpadded -Wno-gnu-empty-initializer -O3 -DNDEBUG cv_intersect.c -o cv_intersect; and ./cv_intersect
*/
#include <assert.h>
#include <math.h> /* fabs */
#include <stdio.h>
#include <stdlib.h> /* exit qsort */
#include <time.h> /* clock_t, clock, CLOCKS_PER_SEC */
#include <stdint.h>

#define internal static

#define u8 uint8_t
#define u32 uint32_t
#define u64 uint64_t
#define f32 float
#define f64 double

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

typedef union latlong {
	struct {
		f64 lat;
		f64 lng;
		f64 cos_lat;
       u64 id;
        } d;
	
	f64 ll[2];
} latlong;


#define earth_radius_km  6371.0
#define pi  3.14159265358979
#define one_km_in_radians ((2.0 * pi) / earth_radius_km)
#define km_per_lat  111.325

f64 km_per_lng( latlong ll ) {
        f64 kml = 111.12f * ll.d.cos_lat;
	return kml;
}

// can be optimized by not doing sqrt and replacing pow() with mul
f64 calculate_distance2( latlong from, latlong to ) {
	f64 d = 0;

	f64 m_lng = km_per_lng( from ) * 1000.0f; // does it matter if from or to??? A little!

	f64 m_lat = km_per_lat * 1000.0f;
	
        f64 diff_lng = (from.d.lng-to.d.lng) * m_lng;
        f64 diff_lat = (from.d.lat-to.d.lat) * m_lat;
	
	d = sqrt( diff_lng*diff_lng + diff_lat*diff_lat );
		
	return d;
}

int compare_id(const void * a, const void * b)
{
        return ((latlong*)a)->d.id - ((latlong*)b)->d.id;
}

int compare_lat(const void * a, const void * b)
{
        f32 diff  = ((latlong*)a)->d.lat - ((latlong*)b)->d.lat;
  return diff == 0 ? 0 : ( diff < 0 ? -1 : 1);
}

#define BINSEARCH_ERROR 0
#define BINSEARCH_FOUND 1
#define BINSEARCH_INSERT 2

internal u8 binary_search( latlong* landmarks, u32 num_landmarks, u32 lat_or_long, f64 target, u32* index ) {

	// no data at all
	if( landmarks == NULL ) {
		return BINSEARCH_ERROR;
	}
	
	// empty array, or insert location should be initial element
	if( num_landmarks == 0 || target < landmarks[0].ll[lat_or_long] ) {
		*index = 0;
		return BINSEARCH_INSERT;
	}
	
	u32 span = num_landmarks;
	u32 mid = num_landmarks / 2;
	u32 large_half;
	while( span > 0 ) {

		if( target == landmarks[mid].ll[lat_or_long] ) {
			*index = mid;
			return BINSEARCH_FOUND;
		}
		
		span = span/2; // half the range left over
		large_half = span/2 + (span % 2);// being clever. But this is ceil 

		if( target < landmarks[mid].ll[lat_or_long] ) {
			mid -= large_half;
		} else {
			mid += large_half;
		}
		
	}

	// target_key is not an element of keys, but we found the closest location
	if( mid == num_landmarks ) { // after all other elements
		*index = num_landmarks;
	} else if( target < landmarks[mid].ll[lat_or_long] ) {
		*index = mid; // displace, shift the rest right
	} else if( target > landmarks[mid].ll[lat_or_long] ) {
		*index = mid+1; // not sure if these two are both possible
	} else {
		assert(0); // cannot happen
	}


	// correctness checks:
	// 1. array has elements, and we should insert at the end, make sure the last element is smaller than the new one
	if( num_landmarks > 0 && *index == num_landmarks ) {
		assert( target > landmarks[num_landmarks-1].ll[lat_or_long] );
	}
	// 2. array has no elements (we already check this above, but left for completeness)
	if( num_landmarks == 0 ) {
		assert( *index == 0 );
	}
	// 3. array has elements, and we should insert at the beginning
	if( num_landmarks > 0 && *index == 0 ) {
		assert( target < landmarks[0].ll[lat_or_long]  ); // MUST be smaller, otherwise it would have been found if equal
	}
	// 4. insert somewhere in the middle
	if( *index > 0 && *index < num_landmarks ) {
		assert( target < landmarks[*index].ll[lat_or_long]  ); // insert shifts the rest right, MUST be smaller otherwise it would have been found
		assert( landmarks[*index-1].ll[lat_or_long]  < target ); // element to the left is smaller
	}

	return BINSEARCH_INSERT;
}

void read_csv( char* filename, latlong* items, u32 max_items, u32 *items_count ) {
	FILE *csv;
	csv = fopen( filename, "r" );
	// skip the first line, it has a mysql header thing
        char header[256];
        int scan_count = fscanf( csv, "%s%s%s%s", header, header+20, header+40, header+60);
	// printf("Header: %s\n", header);
        u64 id;
	f64 lat, lng;


	u32 items_read = 0;
	while( 1 ) {
                if ( items_read >= max_items ) {
                    printf("insufficient space: %u : %u\n", items_read, max_items);
                    exit(1);
                };
                scan_count = fscanf( csv, "%llu\t%lf\t%lf\n", &id, &lat, &lng );
                if( scan_count !=3 || feof(csv) ) {
			printf("EOF reached, read %u items from %s\n", items_read, filename);
			break;
		} else {
			latlong L = 
			{
				.d.id = id,
				.d.lat = lat,
				.d.lng = lng,
				.d.cos_lat = cos(lat/360.0f),
			};
			items[items_read] = L; 
			if( items_read % 1000 == 0 ) {
				// printf("\rRead %u\n", items_read);
			}
			items_read++;
		}
	}
	
	fclose( csv );
	
	*items_count = items_read;
}

int main( int argc, char** argv ) {
        if (argc<3) {
            printf("cv_intersect H L\n");
            exit(0);
        }
	clock_t start = clock();

	
	printf("Reading Hotels from %s\n", argv[1]);

	u32 max_hotels = 10 * 1000 * 1000;
	latlong* hotels = (latlong*) malloc( max_hotels * sizeof(latlong) );

	u32 hotel_count;
	read_csv( argv[1], hotels, max_hotels, &hotel_count );


	printf("Reading Landmarks from %s\n", argv[2]);
	
	u32 max_landmarks = 25 * 1000 * 1000;
	latlong* landmarks_by_lat = (latlong*) malloc( max_landmarks * sizeof(latlong) );
	
	u32 landmark_count;
	read_csv( argv[2], landmarks_by_lat, max_landmarks, &landmark_count );
  printf("Time spent reading data %fs\n", (f32)(clock() - start) / (f32)CLOCKS_PER_SEC );


	// read id, long, lat
	// make 1 list:
	// latlong sorted by lat
	
	// then read the others (id, log, lat)
	// find any item in range of the thing in lat, traverse left and right to find all in range
	// and test ones that are also in range of long, for those do distance calc
	
	// dump results in some format
 
  start = clock();
  qsort (landmarks_by_lat, landmark_count, sizeof(latlong), compare_lat);
  printf("Time spent sorting landmarks %fs\n", (f32)(clock() - start) / (f32)CLOCKS_PER_SEC );

#if 0
	  for (u32 n=0; n<10; n++) {
             printf ("%u: [%.2f, %.2f]\n", landmarks_by_lat[n].d.id, landmarks_by_lat[n].d.lat, landmarks_by_lat[n].d.lng);
	  }
#endif	  


	start = clock();

          u32 bs;
          u32 landmark_index = 0;
	  f32 distances_km[] = {50.0f, 25.0f, 10.0f, 5.0f, 2.0f, 1.0f}; 
	  f64 distances_rad[] = {50.0f*one_km_in_radians, 25.0f*one_km_in_radians, 10.0f*one_km_in_radians, 5.0f*one_km_in_radians, 2.0f*one_km_in_radians, 1.0f*one_km_in_radians };
	  
	  assert( ARRAY_SIZE(distances_km) == ARRAY_SIZE(distances_rad) );

	  latlong close_by[100 * 1000]; // assume there are less than 100K landmark close by ever
	  FILE* out = fopen("landmarks_in_range.csv", "w");
	  for (u32 i=0; i<hotel_count; i++) {

		  u32 cb = 0;
                  u32 landmarks_in_distance[ ARRAY_SIZE(distances_km) ] = { 0, 0, 0, 0, 0, 0 };
                  if( i % 10000 == 0 ) {
			  printf("\rProcessing hotels %u/%u %.3f%% (%.0f hotels/sec)", i, hotel_count, 100.0f * (f32)i / (f32)hotel_count, (f32)i / ( (f32)(clock() - start) / (f32)CLOCKS_PER_SEC )) ;
			  fflush(stdout);
		  }

                  bs = binary_search( landmarks_by_lat, landmark_count, 0, hotels[i].d.lat, &landmark_index ); // landmark_index is the index where inserting this lat would keep the array sorted
             // printf ("Closest to H[%.2f, %.2f] = L[%.2f, %.2f] (%u)\n", hotels[i].d.lat, hotels[i].d.lng, landmarks_by_lat[landmark_index].d.lat, landmarks_by_lat[landmark_index].d.lng, landmark_index);
                  if (bs == BINSEARCH_ERROR) {
                      printf("binsearch error\n");
                      exit(0);
                  }

		  u32 up = landmark_index;
		  // printf("Checking increasing distances in rad from %lu to %lu (rad dist: %f)\n", up, num_landmarks, distances_rad[0]);
                  f32 max_dist = hotels[i].d.lat + distances_rad[0];
                  while( up < landmark_count && landmarks_by_lat[up].d.lat < max_dist ) {
				close_by[cb++] = landmarks_by_lat[up];
				up++;
		  }
		  
		  u32 down = landmark_index;
		  // printf("Checking decreasing distances in rad from %lu to %lu (rad dist: %f)\n", up, num_landmarks, distances_rad[0]);
                  max_dist = hotels[i].d.lat - distances_rad[0];
                  while( down > 0 && landmarks_by_lat[down].d.lat > max_dist ) {
				close_by[cb++] = landmarks_by_lat[down];
				down--;
		  }
		  // printf("Total lat candidates: %u\n", cblat);

		  for( u32 c = 0; c<cb; c++ ) {
			  f32 distance = calculate_distance2( close_by[c], hotels[i] );

			  for( u32 d=0; d<ARRAY_SIZE(distances_km); d++ ) { // just rad
				  if( distance <= (distances_km[d] * 1000.0f ) ) {
				  		landmarks_in_distance[d]++;
				  } else {
					  break;
				  }
			  }
		  }

                  fprintf(out, "%llu,%u,%u,%u,%u,%u,%u\n", hotels[i].d.id, landmarks_in_distance[0], landmarks_in_distance[1], landmarks_in_distance[2], landmarks_in_distance[3], landmarks_in_distance[4], landmarks_in_distance[5] );
	  }
	  
	  fclose(out);
  printf("\nTime spent distance finding %fs\n", (f32)(clock() - start) / (f32)CLOCKS_PER_SEC );
	  
	
	exit( 0 );
}
