/*
 * tutorial/tut_5_1.c
 *
 * A single-threaded CPU only version of the AWACS simulation as a baseline for
 * adding CUDA GPGPU physics computing and multithreaded trials.
 *
 * Copyright (c) Asbjørn M. Bonvik 2026.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cimba.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "hdf5.h"

/* Bit masks to distinguish between two types of user-defined logging messages. */
#define USERFLAG1 0x00000001
#define USERFLAG2 0x00000002

/* Radar targets spread uniformly about the map */
#define NUM_TARGETS 1000

/* Sensor update interval, seconds */
const float time_step = 1.0f;

/* Geometric conversion constants */
const double arcsec_to_meters = 30.87;
const double nm_to_meters = 1852.0;
const double feet_to_meters = 0.3048;
const double knots_to_ms = (1852.0 / 3600.0);
const double deg_to_rad = (2.0 * M_PI / 360.0);
const double rad_to_deg = (360.0 / (2.0 * M_PI));

/* WGS84 constants semi-major axis and eccentricity */
const double WGS84_A = 6378137.0;
const double WGS84_F = (1.0 / 298.257223563);
const double WGS84_E2 = (WGS84_F * (2.0 - WGS84_F));

/* Synthetic terrain generation parameters */
const float terrain_max = 2500.0f;
const unsigned int terrain_octaves = 6u;
const float terrain_initfreq = 1.0f / 100000.0f;
const float terrain_ridginess = 1.2f;
const float terrain_peakiness = 1.7f;
const float terrain_stddev = 3.0f;  /* Local small-scale noise */

/* For ParaView visualization */
const char *terrain_h5name = "terrain.vtkhdf";
const char *terrain_xdmfname = "terrain.xmf";
const char *events_h5name = "events.vtkhdf";
const char *events_xdmfname = "events.xmf";

struct target;
struct platform_state;
struct vtkhdf_handle {
    hid_t file_id;
    hid_t pts_dset;
    hid_t status_dset;
    hid_t step_vals_dset;
    hid_t step_pts_off_dset;
    hid_t awacs_pos_dset;
    hid_t awacs_dir_dset;
    hid_t sensor_dir_dset;
    unsigned long current_step;
    int num_targets;
};

void events_vtkhdf_init(struct vtkhdf_handle *h, const char *filename, int n_targets);
void events_vtkhdf_close(const struct vtkhdf_handle *h);
void events_vtkhdf_append(struct vtkhdf_handle *h,
                   const struct target *targets,
                   const struct platform_state *awacs,
                   float sensor_dir,
                   float time_val);
void events_write_xdmf(const char *xmf_filename, const char *h5_filename,
                       unsigned long num_steps, unsigned num_targets);
void terrain_vtkhdf_write(const char *h5_filename, const float *map,
                          unsigned cols, unsigned rows,
                          float x_scale, float y_scale);
void terrain_xdmf_write(const char *xmf_filename, const char *h5_filename,
                        unsigned cols, unsigned rows,
                        float x_scale, float y_scale);

/* Utility function for user entertainment */
void progress_bar_update(unsigned int current, unsigned int total, time_t start_time);

/*******************************************************************************
 * The terrain model, altitudes in meters referred to a Cartesian plane touching
 * Earth at the reference position.
 */
struct terrain {
    float ref_lat_r;    /* Location of terrain reference point, radians latitude */
    float ref_lon_r;    /* Location of terrain reference point, radians longitude */
    float rad_eff;      /* Effective Earth radius at ref loc, using a 4/3 radar earth model */
    float x_scale;      /* Meters per arcsecond longitude */
    float y_scale;      /* Meters per arcsecond latitude */
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    unsigned int cols;
    unsigned int rows;
    float *map;         /* The cols x rows matrix of elevations spaced by one arcsecond */
    int p[512];         /* The Perlin blueprint */
};

/* Allocate memory for the terrain object */
struct terrain *terrain_create(void)
{
    struct terrain *tp = malloc(sizeof(struct terrain));
    cmb_assert_release(tp != NULL);

    tp->cols = 0;
    tp->rows = 0;
    tp->map = NULL;

    return tp;
}

/* Simple fade function for smooth transitions: 6t^5 - 15t^4 + 10t^3 */
static float terrain_fade(const float t)
{
    return t * t * t * (t * (t * 6 - 15) + 10);
}

/* Linear interpolation */
static float terrain_lerp(const float t, const float a, const float b)
{
    return a + t * (b - a);
}

/* A simple hash to turn grid coordinates into a gradient direction */
static float terrain_grad(const int hash, const float x, const float y)
{
    const int h = hash & 15;
    const float u = h < 8 ? x : y;
    const float v = h < 4 ? y : h == 12 || h == 14 ? x : 0;

    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

/* Generate, shuffle, and duplicate the Perlin blueprint */
static void terrain_generate_blueprint(struct terrain *tp)
{
    for (int i = 0; i < 256; i++) {
        tp->p[i] = i;
    }

    for (int i = 255; i > 0; i--) {
        const int j = (int)cmb_random_uniform(0, i + 1);
        const int temp = tp->p[i];
        tp->p[i] = tp->p[j];
        tp->p[j] = temp;
    }

    for (int i = 0; i < 256; i++) {
        tp->p[256 + i] = tp->p[i];
    }
}

/* 2D Perlin noise function */
float terrain_perlin_noise2d(const struct terrain *tp, float x, float y)
{
    const int X = (int)floorf(x) & 255;
    const int Y = (int)floorf(y) & 255;

    x -= floorf(x);
    y -= floorf(y);

    const float u = terrain_fade(x);
    const float v = terrain_fade(y);

    const int A  = tp->p[X] + Y;
    const int AA = tp->p[A];
    const int AB = tp->p[A + 1];

    const int B  = tp->p[X + 1] + Y;
    const int BA = tp->p[B];
    const int BB = tp->p[B + 1];

    return terrain_lerp(v,
        terrain_lerp(u, terrain_grad(tp->p[AA], x, y),
                        terrain_grad(tp->p[BA], x - 1, y)),
        terrain_lerp(u, terrain_grad(tp->p[AB], x, y - 1),
                        terrain_grad(tp->p[BB], x - 1, y - 1)));
}

/*
 * Generate the terrain model relative to a flat plane using Perlin noise.
 * The plane touches the WGS84 ellipsoid at the reference location, given in degrees lat, lon.
 * The map is tsz_w nautical miles wide, tsw_h nautical miles tall.
 * Likely to take a while, so we entertain the user with a progress bar in the meanwhile.
 */
void terrain_init(struct terrain *tp,
                  const float tsz_w, const float tsz_h,
                  const float ref_lat, const float ref_lon)
{
    cmb_assert_release(tp != NULL);
    cmb_assert_release(tsz_w > 0);
    cmb_assert_release(tsz_h > 0);

    printf("Initializing terrain map of %3.0f x %3.0f nm centered on pos %5.2fN, %5.2fE\n",
            tsz_w, tsz_h, ref_lat, ref_lon);

    tp->cols = (unsigned int)(tsz_w * nm_to_meters / arcsec_to_meters);
    tp->rows = (unsigned int)(tsz_h * nm_to_meters / arcsec_to_meters);
    tp->ref_lat_r = (float)deg_to_rad * ref_lat;
    tp->ref_lon_r = (float)deg_to_rad * ref_lon;

    /* Map resolution one arcsecond, calculate as meters at ref pos on WGS84 ellipsoid */
    const double sin_lat = sinf(tp->ref_lat_r);
    const double cos_lat = cosf(tp->ref_lat_r);
    const double common = 1.0 - (WGS84_E2 * sin_lat * sin_lat);
    const double sqrt_common = sqrt(common);
    const double loc_radius_ew = WGS84_A / sqrt_common;
    const double m_per_deg_ew = loc_radius_ew * cos_lat * (M_PI / 180.0);
    tp->x_scale = (float)((1.0 / 3600.0) * m_per_deg_ew);
    const double loc_radius_ns = WGS84_A * (1.0 - WGS84_E2) / (common * sqrt_common);
    const double m_per_deg_ns = loc_radius_ns * (M_PI / 180.0);
    tp->y_scale = (float)((1.0 / 3600.0) * m_per_deg_ns);

    const float x_span = (float)(tp->cols - 1) * tp->x_scale;
    const float y_span = (float)(tp->rows - 1) * tp->y_scale;
    tp->x_min = -(x_span / 2.0f);
    tp->x_max = (x_span / 2.0f);
    tp->y_min = -(y_span / 2.0f);
    tp->y_max = (y_span / 2.0f);

    /* Calculating the effective radius for 4/3 Earth radar horizon */
    const double loc_radius_mean = sqrt(loc_radius_ew * loc_radius_ns);
    tp->rad_eff = (float)(loc_radius_mean * (4.0 / 3.0));

    printf("Generating %d x %d terrain grid...\n", tp->cols, tp->rows);
    tp->map = cmi_calloc(tp->cols * tp->rows, sizeof(float));

    const time_t start = time(NULL);
    struct cmb_datasummary tds = {};
    cmb_datasummary_initialize(&tds);

    terrain_generate_blueprint(tp);
    for (unsigned row = 0; row < tp->rows; row++) {
        /* Row is north-south (Y axis) */
        if (row % 100 == 0) {
            progress_bar_update(row, tp->rows, start);
        }
        const float ys = ((float)row - (float)tp->rows / 2.0f) * tp->y_scale;

        for (unsigned col = 0; col < tp->cols; col++) {
            /* Col is east-west (X axis) */
            const float xs = ((float)col - (float)tp->cols / 2.0f) * tp->x_scale;

            float h = 0.0f;
            float freq = terrain_initfreq;
            float amp = 1.0f;
            float weight = 1.0f;
            float ampsum = 0.0f;

            for (unsigned i = 0; i < terrain_octaves; i++) {
                float n = terrain_perlin_noise2d(tp, xs * freq, ys * freq);
                n = powf(1.0f - fabsf(n), terrain_ridginess);
                h += n * amp * weight;
                weight = n;
                freq *= 2.05f;
                ampsum += amp;
                amp  *= 0.5f;
            }

            h /= ampsum;
            h = powf(h, terrain_peakiness);
            float h_sum = (h * terrain_max) + (float)cmb_random_normal(0.0, terrain_stddev);
            h_sum = (h_sum < 0.0f) ? 0.0f : h_sum;
            tp->map[row * tp->cols + col] = h_sum;

            cmb_datasummary_add(&tds, h_sum);
        }
    }

    /* End progress bar at 100 % exactly */
    progress_bar_update(tp->rows, tp->rows, start);

    printf("\nDone, terrain characteristics:\n");
    printf("Number of points:   %4.0f million\n", (float)cmb_datasummary_count(&tds)/1.0e6);
    printf("Minimum elevation:  %4.0f meters\n", cmb_datasummary_min(&tds));
    printf("Maximum elevation:  %4.0f meters\n", cmb_datasummary_max(&tds));
    printf("Average elevation:  %4.0f meters\n", cmb_datasummary_mean(&tds));
    printf("Standard deviation: %4.0f meters\n", cmb_datasummary_stddev(&tds));
    printf("Size in memory:     %4.2f GB\n",
              (float)(tp->cols * tp->rows * sizeof(float)) / 1024 / 1024 / 1024.0f);
    cmb_datasummary_terminate(&tds);

    printf("Writing terrain map to ParaView file %s...", terrain_h5name);
    fflush(stdout);
    terrain_vtkhdf_write(terrain_h5name, tp->map, tp->cols, tp->rows, tp->x_scale, tp->y_scale);
    terrain_xdmf_write(terrain_xdmfname, terrain_h5name, tp->cols, tp->rows, tp->x_scale, tp->y_scale);
    printf(" done\n\n");
}

void terrain_terminate(struct terrain *tp)
{
    cmb_assert_release(tp != NULL);
    cmb_assert_release(tp->map != NULL);

    cmi_free(tp->map);
    tp->map = NULL;
}

void terrain_destroy(struct terrain *tp)
{
    cmb_assert_release(tp != NULL);
    cmb_assert_release(tp->map == NULL);

    cmi_free(tp);
}

/* Lookup function to find the terrain array index corresponding to a given (x,y) position */
unsigned terrain_index(const struct terrain *tp, const float x, const float y)
{
    cmb_assert_release(tp != NULL);
    cmb_assert_release((x >= tp->x_min) && (x <= tp->x_max));
    cmb_assert_release((y >= tp->y_min) && (y <= tp->y_max));

    const int raw_col = (int)roundf(x / tp->x_scale) + (int)(tp->cols / 2);
    const int raw_row = (int)roundf(y / tp->y_scale) + (int)(tp->rows / 2);

    const unsigned col = (unsigned)((raw_col < 0) ? 0 : (raw_col >= (int)tp->cols ? (int)tp->cols - 1 : raw_col));
    const unsigned row = (unsigned)((raw_row < 0) ? 0 : (raw_row >= (int)tp->rows ? (int)tp->rows - 1 : raw_row));

    const unsigned index = row * tp->cols + col;
    cmb_assert_debug(index < tp->cols * tp->rows);

    return index;
}

float terrain_elevation(const struct terrain *tp, const float x, const float y)
{
    cmb_assert_release(tp != NULL);

    const unsigned index = terrain_index(tp, x, y);

    return tp->map[index];
}

/****************************************************************************
 * Radar targets spread over the map surface
 */
enum target_mode {
    LO_OBS,
    HI_OBS
};

enum target_detect_state {
    UNDETERMINED,
    HIDING,
    BEYOND_HORIZON,
    OUTSIDE_VERTICAL,
    TERRAIN_SHIELDED,
    MISSED,
    DETECTED
};

struct target {
    /* Active process parent class */
    struct cmb_process core;
    /* Inherent parameters */
    float cross_section;
    float reflectivity;
    float height;
    struct terrain *terrain;
    /* Instantaneous state */
    enum target_mode mode;
    enum target_detect_state tds;
    double time_s;
    float x_m;
    float y_m;
    float alt_m;
    float dir_r;
    float vel_ms;
    /* Cumulative state */
    bool detected;
};

void *target_proc(struct cmb_process *proc, void *vctx)
{
    cmb_assert_release(proc != NULL);
    cmb_assert_release(vctx != NULL);

    struct terrain *terp = (struct terrain *)vctx;
    struct target *tgt = (struct target *)proc;

    /* Pick a location at random, hide there */
    tgt->time_s = cmb_time();
    tgt->x_m = (float)cmb_random_uniform(terp->x_min, terp->x_max);
    tgt->y_m = (float)cmb_random_uniform(terp->y_min, terp->y_max);
    tgt->alt_m = terrain_elevation(terp, tgt->x_m, tgt->y_m) + tgt->height;

    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        /* Stay hidden for a random time */
        tgt->mode = LO_OBS;
        tgt->tds = HIDING;
        tgt->time_s = cmb_time();
        tgt->vel_ms = 0.0f;
        cmb_process_hold(cmb_random_exponential(3600.0));

        /* Choose a direction and speed at random, move for a random time */
        tgt->mode = HI_OBS;
        tgt->tds = UNDETERMINED;
        tgt->time_s = cmb_time();
        tgt->dir_r = (float)cmb_random_uniform(0.0, 2.0 * M_PI);
        tgt->vel_ms = (float)cmb_random_uniform(5.0, 20.0);
        cmb_process_hold(cmb_random_exponential(3600.0));
    }
}

struct target *target_create(void)
{
    struct target *tgt = cmi_malloc(sizeof(struct target));

    return tgt;
}

void target_initialize(struct target *tgt,
                       const char *tgt_name,
                       const float cross_section,
                       const float reflectivity,
                       const float altitude,
                       struct terrain *terp)
{
    cmb_assert_release(tgt != NULL);

    tgt->cross_section = cross_section;
    tgt->reflectivity = reflectivity;
    tgt->height = altitude;
    tgt->terrain = terp;
    tgt->mode = LO_OBS;
    tgt->tds = UNDETERMINED;
    tgt->time_s = 0.0;
    tgt->x_m = 0.0f;
    tgt->y_m = 0.0f;
    tgt->alt_m = 0.0f;
    tgt->dir_r = 0.0f;
    tgt->vel_ms = 0.0f;
    tgt->detected = false;

    cmb_process_initialize((struct cmb_process *)tgt, tgt_name, target_proc, terp, 0);
}

void target_terminate(struct target *tgt)
{
    cmb_assert_release(tgt != NULL);
    cmb_process_terminate((struct cmb_process *)tgt);
}

void target_destroy(struct target *tgt)
{
    cmb_assert_release(tgt != NULL);

    cmi_free(tgt);
}

/* Position update function, called by sensor */
static void target_position_update(struct target *tgt)
{
    cmb_assert_release(tgt != NULL);

    const double dt = cmb_time() - tgt->time_s;
    float x = tgt->x_m + (float)(dt * tgt->vel_ms * cosf(tgt->dir_r));
    if (x > tgt->terrain->x_max) {
        const float overshoot = x - tgt->terrain->x_max;
        x = tgt->terrain->x_min + overshoot;
    }
    else if (x < tgt->terrain->x_min) {
        const float overshoot = tgt->terrain->x_min - x;
        x = tgt->terrain->x_max - overshoot;
    }

    float y = tgt->y_m + (float)(dt * tgt->vel_ms * sinf(tgt->dir_r));
    if (y > tgt->terrain->y_max) {
        const float overshoot = y - tgt->terrain->y_max;
        y = tgt->terrain->y_min + overshoot;
    }
    else if (y < tgt->terrain->y_min) {
        const float overshoot = tgt->terrain->y_min - y;
        y = tgt->terrain->y_max - overshoot;
    }

    const float alt = terrain_elevation(tgt->terrain, x, y) + tgt->height;

    tgt->time_s = cmb_time();
    tgt->x_m = x;
    tgt->y_m = y;
    tgt->alt_m = alt;
}

/*
 * 1. Swept Sector Check
 * Normalizes angles to safely check if the target's azimuth
 * falls within the arc swept during this time step.
 */
static bool target_is_in_swept_sector(const float prev_dir,
                                      const float sweep_width,
                                      const float tgt_azi)
{
    float rel_azi = tgt_azi - prev_dir;

    /* Normalize to [0, 2*PI) */
    while (rel_azi < 0.0f) rel_azi += 2.0f * (float)M_PI;
    while (rel_azi >= 2.0f * (float)M_PI) rel_azi -= 2.0f * (float)M_PI;

    /* If the relative angle is less than the total swept width, it was hit */
    return (rel_azi <= sweep_width);
}

/*
 * 2. Radar Horizon Check (4/3 Earth Approximation)
 */
static bool target_is_beyond_horizon(const float d_2d, const float h_sensor,
                                     const float h_tgt, const float r_eff)
{
    /* Prevent negative sqrt if below sea level */
    const float hs = fmaxf(0.0f, h_sensor);
    const float ht = fmaxf(0.0f, h_tgt);

    const float max_dist = sqrtf(2.0f * r_eff * hs) + sqrtf(2.0f * r_eff * ht);

    return (d_2d > max_dist);
}

/*
 * 3. Nadir Hole Check with Roll Compensation
 */
static bool target_is_in_vertical(const float dx, const float dy, const float dz,
                                  const float d_2d, const float platform_hdg,
                                  const float platform_roll,
                                  const float min_elev, const float max_elev)
{
    const float tgt_azi = atan2f(dy, dx);
    const float rel_brg = tgt_azi - platform_hdg;

    /* True geometric elevation angle to the target */
    const float geom_elev = atan2f(dz, d_2d);

    /* Apparent elevation relative to the sensor's tilted frame.
       If rolling right (positive), a target on the right (sin(rel_brg) > 0)
       moves "up" in the sensor's FoV, effectively lowering the nadir hole for that side. */
    const float apparent_elev = geom_elev + (platform_roll * sinf(rel_brg));

    return ((apparent_elev >= min_elev) && (apparent_elev <= max_elev));
}

/*
 * 4. Terrain Ray-Marching (Stub)
 */
struct sensor;
static bool target_check_terrain_shielding(struct sensor *senp, struct target *tgt) {
    /* TODO: Implement step-by-step ray-marching against tgt->terrain heightmap.
       Return true if line-of-sight is broken. */
    return false;
}

/*
 * 5. Probabilistic Detection (Stub)
 */
static bool target_attempt_detection(struct sensor *senp, struct target *tgt, float d_3d) {
    /* TODO: Radar equation. Calculate SNR based on d_3d, tgt->cross_section,
       and ground clutter based on geometric elevation. */

    /* Dummy placeholder: 90% chance of detection */
    float p = 0.9f;

    return cmb_random_bernoulli(p);
}

/****************************************************************************
 * Geometry of the AWACS racetrack orbit
 */
struct racetrack {
    /* Parameters */
    float start_time;      /* Simulation time when first at anchor point */
    float anchor_lat_r;    /* Startpoint for hot leg, radians latitude */
    float anchor_lon_r;    /* Startpoint for hot leg, radians longitude */
    float orientation_r;   /* Direction of hot leg, radians from north  */
    float length_m;        /* Length of each leg in meters */
    float turn_radius_m;   /* In meters */
    float altitude_m;      /* In meters */
    float velocity_ms;      /* In meters per second */
    bool clockwise;         /* true for standard orbit, false for non-standard */

    /* Pre-calculated from parameters*/
    float turn_dist_m;     /* Length of turn perimeter, meters */
    float orbit_dist_m;    /* Length of entire orbit, meters */
    float orbit_time_s;    /* Duration of a full orbit, seconds */
    float side_multiplier; /* 1.0 for clockwise, -1.0 for anti-clockwise */
    float lat_r2m;         /* Local conversion radians to meters, latitude */
    float lon_r2m;         /* Local conversion radians to meters, longitude */
    float roll_angle_r;    /* Platform banking angle during turns, radians */
};

struct racetrack *racetrack_create(void) {
    struct racetrack *rt = cmi_malloc(sizeof(struct racetrack));

    return rt;
}

void racetrack_initialize(struct racetrack *rt,
                          const float start_time,      /* Simulation time */
                          const float anchor_lat,      /* Degrees */
                          const float anchor_lon,      /* Degrees */
                          const float orientation,     /* Degrees */
                          const float length,          /* Nautical miles */
                          const float turn_radius,     /* Nautical miles */
                          const float flight_level,    /* FL; feet / 100 */
                          const float velocity,        /* Knots */
                          const bool clockwise) {
    cmb_assert_release(rt != NULL);

    rt->start_time = start_time;
    rt->anchor_lat_r = (float)(anchor_lat * deg_to_rad);
    rt->anchor_lon_r = (float)(anchor_lon * deg_to_rad);
    rt->orientation_r = (float)(orientation * deg_to_rad);
    rt->length_m = (float)(length * nm_to_meters);
    rt->turn_radius_m = (float)(turn_radius * nm_to_meters);
    rt->altitude_m = (float)(flight_level * 100.0 * feet_to_meters);
    rt->velocity_ms = (float)(velocity * knots_to_ms);
    rt->clockwise = clockwise;

    rt->turn_dist_m = M_PI * rt->turn_radius_m;
    rt->orbit_dist_m = 2.0f * (rt->length_m + rt->turn_dist_m);
    rt->orbit_time_s = rt->orbit_dist_m / rt->velocity_ms;
    rt->side_multiplier = rt->clockwise ? 1.0f : -1.0f;

    /* WGS84 calculation of meters per degree at anchor point */
    const double sin_lat = sinf(rt->anchor_lat_r);
    const double cos_lat = cosf(rt->anchor_lat_r);

    const double common = 1.0 - (WGS84_E2 * sin_lat * sin_lat);
    const double sqrt_common = sqrt(common);

    /* Radius of curvature in the meridian (for north-south) */
    const double M = WGS84_A * (1.0 - WGS84_E2) / (common * sqrt_common);

    /* Radius of curvature in the prime vertical (for east-west) */
    const double N = WGS84_A / sqrt_common;

    /* Add flight altitude for a correct conversion rad => m at that level */
    rt->lat_r2m = (float)(M + rt->altitude_m);
    rt->lon_r2m = (float)((N + rt->altitude_m) * cos_lat);

    /* Calculate the roll angle for a coordinated turn */
    const double g = 9.80665; /* m/s^2 */
    const double roll_mag = atan((rt->velocity_ms * rt->velocity_ms) / (rt->turn_radius_m * g));

    /* Positive roll for a right turn (clockwise), negative for a left turn */
    rt->roll_angle_r = (float)(roll_mag * rt->side_multiplier);
    printf("Racetrack bank angle %3.1f deg\n", rt->roll_angle_r * rad_to_deg);
    printf("Racetrack orbit duration %5.0f seconds\n", rt->orbit_time_s);
    printf("\n");
}

void racetrack_terminate(struct racetrack *rt)
{
    cmb_unused(rt);
}

void racetrack_destroy(struct racetrack *rt) {
    cmb_assert_release(rt != NULL);

    cmi_free(rt);
}

void racetrack_write_vtp(const struct racetrack *rt, const char *filename);

/******************************************************************************
 *  The sensor and the platform carrying it. The sensor is the active component,
 *  while the platform just recalculates its state whenever it is asked to.
 *  Internal SI units, i.e., meters, radians, seconds
 */
struct platform_state {
    float x;
    float y;
    float dir;
    float rol;
    float vel;
    float alt;
};

static void platform_state_update(struct platform_state *state,
                           const struct racetrack *rt,
                           const double t)
{
    const double delta_t = t - rt->start_time;
    double d = fmod(delta_t * rt->velocity_ms, rt->orbit_dist_m);
    if (d < 0) {
        d += rt->orbit_dist_m;
    }

    double x_local, y_local, heading_local, roll_local;

    /* What segment, and where in that segment? */
    if (d < rt->length_m) {
        x_local = 0.0;
        y_local = d;
        heading_local = 0.0;
        roll_local = 0.0;
    }
    else if (d < rt->length_m + rt->turn_dist_m) {
        const double phi = (d - rt->length_m) / rt->turn_radius_m;
        x_local = rt->side_multiplier * rt->turn_radius_m * (1.0 - cos(phi));
        y_local = rt->length_m + rt->turn_radius_m * sin(phi);
        heading_local = phi * rt->side_multiplier;
        roll_local = rt->roll_angle_r;
    }
    else if (d < 2.0 * rt->length_m + rt->turn_dist_m) {
        const double d_seg = d - (rt->length_m + rt->turn_dist_m);
        x_local = rt->side_multiplier * 2.0 * rt->turn_radius_m;
        y_local = rt->length_m - d_seg;
        heading_local = M_PI;
        roll_local = 0.0f;
    }
    else {
        const double phi = (d - (2.0 * rt->length_m + rt->turn_dist_m)) / rt->turn_radius_m;
        x_local = rt->side_multiplier * rt->turn_radius_m * (1.0 + cos(phi));
        y_local = -rt->turn_radius_m * sin(phi);
        heading_local = M_PI + phi * rt->side_multiplier;
        roll_local = rt->roll_angle_r;
    }

    /* Rotate to orbit orientation */
    const double rad_o = rt->orientation_r;
    const double cos_o = cos(rad_o);
    const double sin_o = sin(rad_o);

    /* Rotate local (x,y) by the orbit orientation */
    const double x_final = x_local * cos_o + y_local * sin_o;
    const double y_final = -x_local * sin_o + y_local * cos_o;

    /* Apply cached WGS84 scales at anchor point */
    state->x = (float)x_final;
    state->y = (float)y_final;
    state->dir = (float)fmod(heading_local + rt->orientation_r + 2.0 * M_PI, 2.0 * M_PI);
    state->rol = (float)roll_local;
    state->vel = (float)rt->velocity_ms;
    state->alt = (float)rt->altitude_m;
}

struct platform {
    struct platform_state state;
    struct sensor *radar;
    struct racetrack *orbit;
};

struct platform *platform_create(void)
{
    struct platform *pfp = cmi_malloc(sizeof(struct platform));

    return pfp;
}

void platform_initialize(struct platform *pfp)
{
    cmb_assert_release(pfp != NULL);
    pfp->state.x = 0.0f;
    pfp->state.y = 0.0f;
    pfp->state.dir = 0.0f;
    pfp->state.rol = 0.0f;
    pfp->state.vel = 0.0f;
    pfp->state.alt = 0.0f;
    pfp->radar = NULL;
    pfp->orbit = NULL;
}

void platform_terminate(struct platform *pfp)
{
    cmb_unused(pfp);
}

void platform_destroy(struct platform *pfp)
{
    cmb_assert_release(pfp != NULL);
    cmi_free(pfp);
}

/*
 * Calculate the state of the platform at current simulation time
 */
static void platform_update(struct platform *pfp)
{
    const struct racetrack *rt = pfp->orbit;
    const double t = cmb_time();
    struct platform_state *state = &(pfp->state);
    platform_state_update(state, rt, t);
}

struct sensor {
    struct cmb_process proc;    /* Inheritance, not a pointer */
    struct platform *host;
    float cur_dir;              /* Relative to platform axis */
    float rpm;
    float elev_angle_min;
    float elev_angle_max;
    struct vtkhdf_handle hdf;
 };

void *sensor_proc(struct cmb_process *me, void *vctx)
{
    cmb_assert_release(me != NULL);
    cmb_assert_release(vctx != NULL);

    struct target *targets = vctx;
    /* Assume that all targets are in the same terrain */
    struct terrain *terp = targets[0].terrain;
    struct sensor *senp = (struct sensor *)me;
    struct platform *host = senp->host;
    const float rot_inc = (float)(senp->rpm * (time_step / 60.0f) * (2.0f * M_PI));

    // ReSharper disable once CppDFAEndlessLoop
    while (true) {
        const float prev_hdg = host->state.dir;
        const float prev_sensor_dir = senp->cur_dir;

        platform_update(host);

        /* Calculate total swept angle for this tick */
        const float ddir = host->state.dir - prev_hdg;
        float sweep_width = rot_inc + ddir;

        /* Advance and normalize the sensor direction */
        senp->cur_dir += sweep_width;
        while (senp->cur_dir >= 2.0f * (float)M_PI) senp->cur_dir -= 2.0f * (float)M_PI;
        while (senp->cur_dir < 0.0f) senp->cur_dir += 2.0f * (float)M_PI;

        /* If platform turned against the antenna violently, ensure positive sweep width */
        if (sweep_width < 0.0f) sweep_width = 0.01f;

        /* --- TARGET DETECTION PIPELINE --- */
        for (int i = 0; i < NUM_TARGETS; i++) {
            struct target *tgt = &(targets[i]);

            /* 1. Is it completely hiding? */
            if (tgt->tds == HIDING) {
                continue;
            }

            /* Calculate physics */
            target_position_update(tgt);
            const float dx = tgt->x_m - host->state.x;
            const float dy = tgt->y_m - host->state.y;
            const float dz = tgt->alt_m - host->state.alt;
            const float d_2d = sqrtf(dx*dx + dy*dy);
            const float d_3d = sqrtf(d_2d*d_2d + dz*dz);
            const float tgt_azi = atan2f(dy, dx);

            /* 2. In swept sector? (Clockwise evaluation) */
            if (!target_is_in_swept_sector(prev_sensor_dir, sweep_width, tgt_azi)) {
                continue; /* Outside sector, remains unchanged */
            }

            /* 3. Beyond Radar Horizon? */
            if (target_is_beyond_horizon(d_2d, host->state.alt, tgt->alt_m, terp->rad_eff)) {
                tgt->tds = BEYOND_HORIZON;
                continue;
            }

            /* 4. In Nadir Hole? */
            if (!target_is_in_vertical(dx, dy, dz, d_2d,
                                       host->state.dir, host->state.rol,
                                       senp->elev_angle_min, senp->elev_angle_max)) {
                tgt->tds = OUTSIDE_VERTICAL;
                continue;
            }

            /* 5. Terrain Shielded? */
            if (target_check_terrain_shielding(senp, tgt)) {
                tgt->tds = TERRAIN_SHIELDED;
                continue;
            }

            /* 6. Radar Equation & Probability */
            if (target_attempt_detection(senp, tgt, d_3d)) {
                tgt->tds = DETECTED;
                tgt->detected = true; /* Cumulative flag */
            }
            else {
                tgt->tds = MISSED;
            }
        }

        events_vtkhdf_append(&(senp->hdf), targets, &(host->state),
                      senp->cur_dir, (float)cmb_time());

        cmb_process_hold((double)time_step);
    }
}

struct sensor *sensor_create(void)
{
    struct sensor *senp = cmi_malloc(sizeof(struct sensor));

    return senp;
}

void sensor_initialize(struct sensor *senp, const char *name, const float rpm,
                       const float min_elev, const float max_elev,
                       struct platform *host, void *vctx)
{
    cmb_assert_release(senp != NULL);
    cmb_assert_release(host != NULL);
    cmb_assert_release(senp != NULL);

    senp->host = host;
    senp->cur_dir = 0.0f;
    senp->rpm = rpm;
    senp->elev_angle_min = min_elev;
    senp->elev_angle_max = max_elev;

    events_vtkhdf_init(&(senp->hdf), events_h5name, NUM_TARGETS);

    cmb_process_initialize((struct cmb_process *)senp, name, sensor_proc, vctx, 0);
}

void sensor_terminate(struct sensor *senp)
{
    events_vtkhdf_close(&(senp->hdf));
    events_write_xdmf(events_xdmfname, events_h5name,
        senp->hdf.current_step, senp->hdf.num_targets);

    cmb_process_terminate((struct cmb_process *)senp);
}

void sensor_destroy(struct sensor *senp)
{
    free(senp);
}

/*
 * Our simulated world consists of these entities.
 */
struct simulation {
    struct platform *AWACS;
    struct target *targets;
};

/*
 * A single trial is defined by these parameters and generates these results.
 */
struct trial {
    struct terrain *terrain;
    double duration;
    /* TODO: Place your results here */
    uint64_t seed_used;
};

/*
 * Event to close down the simulation.
 */
void end_sim(void *subject, void *object)
{
    cmb_unused(object);

    const struct simulation *sim = subject;
    struct target *tgts = sim->targets;
    cmb_logger_user(stdout, USERFLAG1, "--- Game Over ---");

    cmb_process_stop((struct cmb_process *)(sim->AWACS->radar), NULL);
    for (unsigned ui = 0; ui < NUM_TARGETS; ui++) {
        cmb_process_stop((struct cmb_process *)&tgts[ui], NULL);
    }
}

const char *hhhmmss_formatter(double t);

/*
 * The simulation driver function to execute one trial
 */
void run_trial(void *vtrl)
{
    cmb_assert_release(vtrl != NULL);
    struct trial *trl = vtrl;
    struct simulation sim = {};

    /* Set up our trial housekeeping */
    printf("Initializing event queue and random generators\n");
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    // cmb_logger_flags_off(USERFLAG1);
    cmb_event_queue_initialize(0.0);
    trl->seed_used = cmb_random_hwseed();
    cmb_random_initialize(trl->seed_used);
    cmb_logger_timeformatter_set(hhhmmss_formatter);

    /* Create and start the targets */
    sim.targets = cmi_calloc(NUM_TARGETS, sizeof(struct target));
    for (unsigned ui = 0; ui < NUM_TARGETS; ui++) {
        char namebuf[16];
        snprintf(namebuf, sizeof(namebuf), "Target_%d", ui);
        target_initialize(&sim.targets[ui], namebuf, 1.0f, 1.0f, 2.0f, trl->terrain);
        cmb_process_start((struct cmb_process *)&sim.targets[ui]);
    }

    /* Create and start the AWACS */
    printf("Initializing racetrack pattern\n");
    struct racetrack *orbit = racetrack_create();
    const float start_time = 0.0f;
    const float anchor_lat = 30.0f;   /* Degrees */
    const float anchor_lon = -9.9f;   /* Degrees */
    const float orientation = 0.0f;     /* Degrees */
    const float length = 50.0f;          /* Nautical miles */
    const float turn_radius = 10.0f;     /* Nautical miles */
    const float flight_level = 310.0f;        /* Flight level */
    const float velocity = 300.0f;        /* Knots */
    const bool clockwise = true;
    racetrack_initialize(orbit, start_time, anchor_lat, anchor_lon, orientation,
                         length, turn_radius, flight_level, velocity, clockwise);
    racetrack_write_vtp(orbit, "racetrack.vtp");

    printf("Initializing AWACS platform\n");
    struct platform *awacs = platform_create();
    sim.AWACS = awacs;
    platform_initialize(awacs);
    awacs->orbit = orbit;

    printf("Initializing radar\n");
    struct sensor *radar = sensor_create();
    sim.AWACS->radar = radar;
    const float rpm = 6.0f;
    const float max_elev = (float)(60.0 * deg_to_rad);
    const float min_elev = (float)(-20.0 * deg_to_rad);
    sensor_initialize(radar, "Radar", rpm, min_elev, max_elev, awacs, sim.targets);
    cmb_process_start((struct cmb_process *)radar);

    /* Schedule the simulation control events */
    printf("Scheduling end event in %4.1f hours from now\n", trl->duration);
    cmb_event_schedule(end_sim, &sim, NULL, 3600.0 * trl->duration, 0);

    /* Run this trial */
    printf("Running simulation...\n");
    cmb_event_queue_execute();

    /*
     * TODO: Collect your statistics here
     */

    printf("Cleaning up\n");
    racetrack_terminate(orbit);
    racetrack_destroy(orbit);
    sensor_terminate(radar);
    sensor_destroy(radar);
    platform_terminate(awacs);
    platform_destroy(awacs);
    for (unsigned ui = 0; ui < NUM_TARGETS; ui++) {
        target_terminate(&sim.targets[ui]);
    }

    cmi_free(sim.targets);

    /* Final housekeeping to leave everything as we found it */
    cmb_event_queue_terminate();
    cmb_random_terminate();

}

/*
 * The minimal single-threaded main function
 */
int main(void)
{
    /* Allocate and generate the terrain map to be used for several trials */
    /* Assume a 100x50 nm racetrack oriented east-west, add 300 nm each way */
    const float fwidth = 100.0f + 2.0f * 300.0f;
    const float fheight = 50.0f + 2.0f * 300.0f;
    const float ref_lat = 30.0f;
    const float ref_lon = -10.0f;

    /* Will generate a new seed in run_trial(), this one is for terrain generation only */
    cmb_random_initialize(cmb_random_hwseed());
    struct terrain *tp = terrain_create();
    terrain_init(tp, fwidth, fheight, ref_lat, ref_lon);

    struct trial trl = {};
    trl.terrain = tp;
    trl.duration = 24.0;

    run_trial(&trl);

    terrain_terminate(tp);
    terrain_destroy(tp);

    return 0;
}

/*******************************************************************************
 * Only I/O utility functions below here
 */

void progress_bar_update(const unsigned int current,
                        const unsigned int total,
                        const time_t start_time)
{
    const unsigned int bar_width = 40;
    const float progress = (float)current / (float)total;
    const unsigned int pos = (unsigned int)((float)bar_width * progress);

    const time_t now = time(NULL);
    const double elapsed = difftime(now, start_time);

    /* Wait until 1 % progress to get a time estimate */
    int eta_sec = 0;
    if (progress > 0.01f) {
        eta_sec = (int)(elapsed / progress - elapsed);
    }

    printf("\r[");
    for (unsigned int i = 0; i < bar_width; ++i) {
        if (i < pos) {
            printf("#");
        }
        else if (i == pos) {
            printf("=");
        }
        else {
            printf("-");
        }
    }

    printf("] %3d%% | ETA: %02d:%02d",
           (int)(progress * 100),
           eta_sec / 60,
           eta_sec % 60);

    fflush(stdout);
}

/* Internal time unit seconds, print in HHH:MM:SS.sss format */
#define FMTBUFLEN 20
char fmtbuf[FMTBUFLEN];

const char *hhhmmss_formatter(const double t)
{
    double tmp = t;
    const unsigned h = (unsigned)(tmp / 3600.0);
    tmp -= (double)(h * 3600);
    const unsigned m = (unsigned)(tmp / 60.0);
    tmp -= (double)(m * 60.0);
    const double s = tmp;
    snprintf(fmtbuf, FMTBUFLEN, "%03d:%02d:%04.1f", h, m, s);

    return fmtbuf;
}

void write_attr_string(const hid_t loc_id, const char *const name, const char *const value)
{
    /* Start with the 1-byte template */
    const hid_t base_type = H5T_C_S1;
    const hid_t custom_type = H5Tcopy(base_type);

    /* Set the size to the exact length of the string plus the null terminator */
    const size_t len = strlen(value);
    const size_t size_with_null = len + 1;
    H5Tset_size(custom_type, size_with_null);

    /* Create the attribute */
    const hid_t space = H5Screate(H5S_SCALAR);
    const hid_t attr = H5Acreate2(loc_id, name, custom_type, space, H5P_DEFAULT, H5P_DEFAULT);

    /* Write using the custom_type so HDF5 knows how many bytes to pull from 'value' */
    H5Awrite(attr, custom_type, value);

    /* Cleanup */
    H5Aclose(attr);
    H5Sclose(space);
    H5Tclose(custom_type);
}

void write_attr_int32_array(const hid_t loc_id, const char *const name, const int32_t *const values, const hsize_t count)
{
    const hid_t space = H5Screate_simple(1, &count, NULL);
    const hid_t type = H5T_NATIVE_INT32;
    const hid_t attr = H5Acreate2(loc_id, name, type, space, H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(attr, type, values);
    H5Aclose(attr);
    H5Sclose(space);
}

void write_attr_double_array(const hid_t loc_id, const char *const name, const double *const values, const hsize_t count)
{
    const hid_t space = H5Screate_simple(1, &count, NULL);
    const hid_t type = H5T_NATIVE_DOUBLE;
    const hid_t attr = H5Acreate2(loc_id, name, type, space, H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(attr, type, values);
    H5Aclose(attr);
    H5Sclose(space);
}

void terrain_vtkhdf_write(const char *const h5_filename,
                          const float *const map,
                          const uint32_t cols,
                          const uint32_t rows,
                          const float x_scale,
                          const float y_scale)
{
    /* 1. Setup File with Latest Version Bounds */
    const hid_t fapl = H5Pcreate(H5P_FILE_ACCESS);
    H5Pset_libver_bounds(fapl, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST);
    const hid_t file_id = H5Fcreate(h5_filename, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
    H5Pclose(fapl);

    /* 2. Create the VTKHDF group */
    const hid_t root = H5Gcreate2(file_id, "VTKHDF", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    /* 3. SHIFT: Instead of PointData, use "CellData" temporarily to see if it bypasses the PointData bug */
    write_attr_string(root, "Type", "ImageData");
    const int32_t version[2] = {2, 0}; // Try Version 2.0 (supported by ParaView 6.0+)
    const hsize_t v_count = 2;
    write_attr_int32_array(root, "Version", version, v_count);

    /* 3. Grid Extent [X_min, X_max, Y_min, Y_max, Z_min, Z_max] */
    /* Force 32-bit signed integers */
    const int32_t extent[6] = {
        (int32_t)0,
        (int32_t)cols - 1,
        (int32_t)0,
        (int32_t)rows - 1,
        (int32_t)0,
        (int32_t)0
    };
    const hsize_t ex_count = 6;
    write_attr_int32_array(root, "WholeExtent", extent, ex_count);

    /* 4. Physical Geometry (Origin, Spacing, Direction) */
    const double total_x = (cols - 1) * (double)x_scale;
    const double total_y = (rows - 1) * (double)y_scale;
    const double origin[3] = { -(total_x / 2.0), -(total_y / 2.0), 0.0 };
    const double spacing[3] = { (double)x_scale, (double)y_scale, 1.0 };
    const double direction[9] = { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };

    const hsize_t three = 3;
    const hsize_t nine = 9;
    write_attr_double_array(root, "Origin", origin, three);
    write_attr_double_array(root, "Spacing", spacing, three);
    write_attr_double_array(root, "Direction", direction, nine);

    /* 5. PointData Group */
    const hid_t pd_group = H5Gcreate2(root, "PointData", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    write_attr_string(pd_group, "Attribute", "Scalars");
    write_attr_string(pd_group, "Scalars", "Elevation");

    /* 6. Dataset Creation with Chunking */
    const hsize_t map_dims[3] = { 1, (hsize_t)rows, (hsize_t)cols };
    const hid_t map_space = H5Screate_simple(3, map_dims, NULL);

    /* Create property list for chunking */
    const hid_t dcpl = H5Pcreate(H5P_DATASET_CREATE);

    /* Define chunk size. A single row (1, 1, cols) is efficient for large grids */
    const hsize_t chunk_dims[3] = { 1, 1, (hsize_t)cols };
    H5Pset_chunk(dcpl, 3, chunk_dims);

    /* Create the dataset with the property list */
    const hid_t map_dset = H5Dcreate2(pd_group, "Elevation", H5T_NATIVE_FLOAT, map_space, H5P_DEFAULT, dcpl, H5P_DEFAULT);

    /* 7. Write Data */
    H5Dwrite(map_dset, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, map);

    /* 8. Cleanup */
    H5Pclose(dcpl);
    H5Dclose(map_dset);
    H5Sclose(map_space);
}

void terrain_xdmf_write(const char *const xmf_filename,
                        const char *const h5_filename,
                        const uint32_t cols,
                        const uint32_t rows,
                        const float x_scale,
                        const float y_scale)
{
    FILE *const fp = fopen(xmf_filename, "w");
    if (!fp) return;

    /* Calculate origin to match your HDF5 centering logic */
    const double total_x = (cols - 1) * (double)x_scale;
    const double total_y = (rows - 1) * (double)y_scale;
    const double ox = -(total_x / 2.0);
    const double oy = -(total_y / 2.0);

    fprintf(fp, "<?xml version=\"1.0\" ?>\n");
    fprintf(fp, "<!DOCTYPE Xdmf SYSTEM \"Xdmf.dtd\" []>\n");
    fprintf(fp, "<Xdmf Version=\"3.0\">\n");
    fprintf(fp, "  <Domain>\n");

    /* 3DCORECTMesh is the XDMF equivalent of ImageData */
    fprintf(fp, "    <Grid Name=\"TerrainGrid\" GridType=\"Uniform\">\n");
    fprintf(fp, "      <Topology TopologyType=\"3DCORECTMesh\" Dimensions=\"1 %u %u\"/>\n", rows, cols);

    fprintf(fp, "      <Geometry GeometryType=\"ORIGIN_DXDYDZ\">\n");
    fprintf(fp, "        <DataItem Name=\"Origin\" Dimensions=\"3\" Format=\"XML\">0.0 %f %f</DataItem>\n", oy, ox);
    fprintf(fp, "        <DataItem Name=\"Spacing\" Dimensions=\"3\" Format=\"XML\">1.0 %f %f</DataItem>\n", (double)y_scale, (double)x_scale);
    fprintf(fp, "      </Geometry>\n");

    fprintf(fp, "      <Attribute Name=\"Elevation\" AttributeType=\"Scalar\" Center=\"Node\">\n");
    fprintf(fp, "        <DataItem Format=\"HDF\" NumberType=\"Float\" Precision=\"4\" Dimensions=\"1 %u %u\">\n", rows, cols);
    /* This path must match your h5dump: /VTKHDF/PointData/Elevation */
    fprintf(fp, "          %s:/VTKHDF/PointData/Elevation\n", h5_filename);
    fprintf(fp, "        </DataItem>\n");
    fprintf(fp, "      </Attribute>\n");

    fprintf(fp, "    </Grid>\n");
    fprintf(fp, "  </Domain>\n");
    fprintf(fp, "</Xdmf>\n");

    fclose(fp);
}

#define H5_CHECK(x) do { \
            hid_t __res = (x); \
            if (__res < 0) { \
                fprintf(stderr, "HDF5 Error at %s:%d (Function: %s)\n", __FILE__, __LINE__, #x); \
                exit(EXIT_FAILURE); \
            } \
        } while (0)

void events_vtkhdf_init(struct vtkhdf_handle *h, const char *filename, const int n_targets)
{
    h->num_targets = n_targets;
    h->current_step = 0;

    h->file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (h->file_id < 0) {
        fprintf(stderr, "Failed to create HDF5 file: %s\n", filename);
        exit(EXIT_FAILURE);
    }

    const hid_t root = H5Gcreate2(h->file_id, "VTKHDF", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(root);
    write_attr_string(root, "Type", "PolyData");

    const int32_t version[2] = {1, 0};
    const hsize_t v_dim = 2;
    const hid_t v_space = H5Screate_simple(1, &v_dim, NULL);
    H5_CHECK(v_space);
    const hid_t v_attr = H5Acreate2(root, "Version", H5T_NATIVE_INT32, v_space, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(v_attr);
    H5Awrite(v_attr, H5T_NATIVE_INT32, version);
    H5Aclose(v_attr);
    H5Sclose(v_space);

    /* --- Primary Point Data (Targets) --- */
    const hsize_t p_dims[2] = {0, 3};
    const hsize_t p_max[2] = {H5S_UNLIMITED, 3};
    const hsize_t p_chunk[2] = {(hsize_t)n_targets, 3};
    const hid_t p_dcpl = H5Pcreate(H5P_DATASET_CREATE);
    H5_CHECK(p_dcpl);
    H5Pset_chunk(p_dcpl, 2, p_chunk);
    const hid_t p_space = H5Screate_simple(2, p_dims, p_max);
    H5_CHECK(p_space);
    H5_CHECK(h->pts_dset = H5Dcreate2(root, "Points", H5T_NATIVE_FLOAT, p_space, H5P_DEFAULT, p_dcpl, H5P_DEFAULT));
    H5Pclose(p_dcpl);
    H5Sclose(p_space);

    /* --- Point Attributes --- */
    const hid_t pd_group = H5Gcreate2(root, "PointData", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(pd_group);
    write_attr_string(pd_group, "Scalars", "DetectStatus");
    const hsize_t s_dims[1] = {0};
    const hsize_t s_max[1] = {H5S_UNLIMITED};
    const hsize_t s_chunk[1] = {(hsize_t)n_targets};
    const hid_t s_dcpl = H5Pcreate(H5P_DATASET_CREATE);
    H5_CHECK(s_dcpl);
    H5Pset_chunk(s_dcpl, 1, s_chunk);
    const hid_t s_space = H5Screate_simple(1, s_dims, s_max);
    H5_CHECK(s_space);
    H5_CHECK(h->status_dset = H5Dcreate2(pd_group, "DetectStatus", H5T_NATIVE_FLOAT,
                                        s_space, H5P_DEFAULT, s_dcpl, H5P_DEFAULT));
    H5Pclose(s_dcpl);
    H5Sclose(s_space);

    /* --- Topology --- */
    const hid_t v_group = H5Gcreate2(root, "Vertices", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(v_group);
    int64_t *conn = malloc(n_targets * sizeof(int64_t));
    int64_t *off = malloc((n_targets + 1) * sizeof(int64_t));
    for (int i = 0; i < n_targets; i++) {
        conn[i] = i;
        off[i] = i;
    }
    off[n_targets] = n_targets;

    const hsize_t c_dim = (hsize_t)n_targets;
    const hsize_t o_dim = (hsize_t)n_targets + 1;
    const hid_t c_space = H5Screate_simple(1, &c_dim, NULL);
    H5_CHECK(c_space);
    const hid_t o_space = H5Screate_simple(1, &o_dim, NULL);
    H5_CHECK(o_space);
    const hid_t c_dset = H5Dcreate2(v_group, "Connectivity", H5T_NATIVE_INT64,
                                    c_space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(c_dset);
    const hid_t o_dset = H5Dcreate2(v_group, "Offsets", H5T_NATIVE_INT64,
                                    o_space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(o_dset);
    H5Dwrite(c_dset, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, conn);
    H5Dwrite(o_dset, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, off);

    free(conn);
    free(off);
    H5Dclose(c_dset);
    H5Dclose(o_dset);
    H5Sclose(c_space);
    H5Sclose(o_space);
    H5Gclose(v_group);

    /* --- Temporal Metadata --- */
    const hid_t st_group = H5Gcreate2(root, "Steps", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(st_group);
    const hsize_t t_dims[1] = {0};
    const hsize_t t_max[1] = {H5S_UNLIMITED};
    const hsize_t t_chunk[1] = {1};
    const hid_t t_dcpl = H5Pcreate(H5P_DATASET_CREATE);
    H5_CHECK(t_dcpl);
    H5Pset_chunk(t_dcpl, 1, t_chunk);
    const hid_t t_space = H5Screate_simple(1, t_dims, t_max);
    H5_CHECK(t_space);
    H5_CHECK(h->step_vals_dset = H5Dcreate2(st_group, "Values", H5T_NATIVE_FLOAT,
            t_space, H5P_DEFAULT, t_dcpl, H5P_DEFAULT));
    H5_CHECK(h->step_pts_off_dset = H5Dcreate2(st_group, "PointsOffsets", H5T_NATIVE_INT64,
            t_space, H5P_DEFAULT, t_dcpl, H5P_DEFAULT));

    H5Pclose(t_dcpl);
    H5Sclose(t_space);
    H5Gclose(st_group);

    /* --- Field Data --- */
    const hid_t fd_group = H5Gcreate2(root, "FieldData", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(fd_group);

    const hsize_t ap_dims[2] = {0, 3};
    const hsize_t ap_max[2] = {H5S_UNLIMITED, 3};
    const hsize_t a_chunk[2] = {1, 3};

    const hid_t ap_dcpl = H5Pcreate(H5P_DATASET_CREATE);
    H5_CHECK(ap_dcpl);
    H5Pset_chunk(ap_dcpl, 2, a_chunk);
    const hid_t ap_space = H5Screate_simple(2, ap_dims, ap_max);
    H5_CHECK(ap_space);

    /* Using absolute paths to force link creation visibility */
    H5_CHECK(h->awacs_pos_dset = H5Dcreate2(h->file_id,
            "/VTKHDF/FieldData/AWACS_Position", H5T_NATIVE_FLOAT, ap_space,
            H5P_DEFAULT, ap_dcpl, H5P_DEFAULT));
    H5_CHECK(h->awacs_dir_dset = H5Dcreate2(h->file_id,
            "/VTKHDF/FieldData/AWACS_Direction", H5T_NATIVE_FLOAT, ap_space,
            H5P_DEFAULT, ap_dcpl, H5P_DEFAULT));
    H5_CHECK(h->sensor_dir_dset = H5Dcreate2(h->file_id,
            "/VTKHDF/FieldData/Sensor_Direction", H5T_NATIVE_FLOAT, ap_space,
            H5P_DEFAULT, ap_dcpl, H5P_DEFAULT));

    H5Sclose(ap_space);
    H5Pclose(ap_dcpl);

    /* Ensure metadata is physically on disk before moving on */
    H5Fflush(h->file_id, H5F_SCOPE_GLOBAL);

    /* Cleanup handles */
    H5Gclose(fd_group);
    H5Gclose(pd_group);
    H5Gclose(root);
}

void events_vtkhdf_append(struct vtkhdf_handle *h,
                   const struct target *targets,
                   const struct platform_state *awacs,
                   const float sensor_dir,
                   const float time_val)
{
    const int n = h->num_targets;
    const hsize_t n_steps = h->current_step + 1;

    /* --- 1. Extend Datasets (With Error Checking) --- */
    const hsize_t p_ext[2] = { n_steps * n, 3 };
    H5_CHECK(H5Dset_extent(h->pts_dset, p_ext));

    const hsize_t s_ext[1] = { n_steps * n };
    H5_CHECK(H5Dset_extent(h->status_dset, s_ext));

    const hsize_t t_ext[1] = { n_steps };
    H5_CHECK(H5Dset_extent(h->step_vals_dset, t_ext));
    H5_CHECK(H5Dset_extent(h->step_pts_off_dset, t_ext));

    const hsize_t vec_ext[2] = { n_steps, 3 };
    H5_CHECK(H5Dset_extent(h->awacs_pos_dset, vec_ext));
    H5_CHECK(H5Dset_extent(h->awacs_dir_dset, vec_ext));
    H5_CHECK(H5Dset_extent(h->sensor_dir_dset, vec_ext));

    /* --- 2. Static Buffers (No Malloc Churn) --- */
    static float p_buf[NUM_TARGETS * 3];
    static float s_buf[NUM_TARGETS];

    for (int i = 0; i < n; i++) {
        p_buf[i * 3 + 0] = targets[i].x_m;
        p_buf[i * 3 + 1] = targets[i].y_m;
        p_buf[i * 3 + 2] = targets[i].alt_m;
        s_buf[i] = (float)targets[i].tds;
    }

    /* --- 3. Write Targets & Status --- */
    const hsize_t p_start[2] = { h->current_step * n, 0 };
    const hsize_t p_count[2] = { (hsize_t)n, 3 };

    const hid_t p_fspace = H5Dget_space(h->pts_dset);
    H5Sselect_hyperslab(p_fspace, H5S_SELECT_SET, p_start, NULL, p_count, NULL);
    const hid_t p_mspace = H5Screate_simple(2, p_count, NULL);
    H5_CHECK(H5Dwrite(h->pts_dset, H5T_NATIVE_FLOAT, p_mspace, p_fspace, H5P_DEFAULT, p_buf));
    H5Sclose(p_fspace);
    H5Sclose(p_mspace);

    const hsize_t s_start[1] = { h->current_step * n };
    const hsize_t s_count[1] = { (hsize_t)n };
    const hid_t s_fspace = H5Dget_space(h->status_dset);
    H5Sselect_hyperslab(s_fspace, H5S_SELECT_SET, s_start, NULL, s_count, NULL);
    const hid_t s_mspace = H5Screate_simple(1, s_count, NULL);
    H5_CHECK(H5Dwrite(h->status_dset, H5T_NATIVE_FLOAT, s_mspace, s_fspace, H5P_DEFAULT, s_buf));
    H5Sclose(s_fspace);
    H5Sclose(s_mspace);

    /* --- 4. Write FieldData (AWACS & Sensor) --- */
    const float ap_buf[3] = { awacs->x, awacs->y, awacs->alt };
    const float ad_buf[3] = { sinf(awacs->dir), cosf(awacs->dir), 0.0f };
    const float sd_buf[3] = { sinf(sensor_dir) * cosf(awacs->rol),
                              cosf(sensor_dir) * cosf(awacs->rol),
                              -sinf(awacs->rol) };

    const hsize_t v_start[2] = { h->current_step, 0 };
    const hsize_t v_count[2] = { 1, 3 };
    const hid_t v_mspace = H5Screate_simple(2, v_count, NULL);

    hid_t fspace = H5Dget_space(h->awacs_pos_dset);
    H5_CHECK(fspace);
    H5Sselect_hyperslab(fspace, H5S_SELECT_SET, v_start, NULL, v_count, NULL);
    H5_CHECK(H5Dwrite(h->awacs_pos_dset, H5T_NATIVE_FLOAT, v_mspace, fspace, H5P_DEFAULT, ap_buf));
    H5Sclose(fspace);

    fspace = H5Dget_space(h->awacs_dir_dset);
    H5_CHECK(fspace);
    H5Sselect_hyperslab(fspace, H5S_SELECT_SET, v_start, NULL, v_count, NULL);
    H5_CHECK(H5Dwrite(h->awacs_dir_dset, H5T_NATIVE_FLOAT, v_mspace, fspace, H5P_DEFAULT, ad_buf));
    H5Sclose(fspace);

    fspace = H5Dget_space(h->sensor_dir_dset);
    H5_CHECK(fspace);
    H5Sselect_hyperslab(fspace, H5S_SELECT_SET, v_start, NULL, v_count, NULL);
    H5_CHECK(H5Dwrite(h->sensor_dir_dset, H5T_NATIVE_FLOAT, v_mspace, fspace, H5P_DEFAULT, sd_buf));
    H5Sclose(fspace);
    H5Sclose(v_mspace);

    /* --- 5. Write Temporal Metadata --- */
    const hsize_t t_start[1] = { h->current_step };
    const hsize_t t_count[1] = { 1 };
    const int64_t pts_off = (int64_t)(h->current_step * n);

    const hid_t tv_fspace = H5Dget_space(h->step_vals_dset);
    H5_CHECK(tv_fspace);
    H5Sselect_hyperslab(tv_fspace, H5S_SELECT_SET, t_start, NULL, t_count, NULL);
    const hid_t tv_mspace = H5Screate_simple(1, t_count, NULL);
    H5_CHECK(tv_mspace);
    H5_CHECK(H5Dwrite(h->step_vals_dset, H5T_NATIVE_FLOAT, tv_mspace, tv_fspace, H5P_DEFAULT, &time_val));
    H5Sclose(tv_fspace); H5Sclose(tv_mspace);

    const hid_t to_fspace = H5Dget_space(h->step_pts_off_dset);
    H5_CHECK(to_fspace);
    H5Sselect_hyperslab(to_fspace, H5S_SELECT_SET, t_start, NULL, t_count, NULL);
    const hid_t to_mspace = H5Screate_simple(1, t_count, NULL);
    H5_CHECK(to_mspace);
    H5_CHECK(H5Dwrite(h->step_pts_off_dset, H5T_NATIVE_INT64, to_mspace, to_fspace, H5P_DEFAULT, &pts_off));
    H5Sclose(to_fspace); H5Sclose(to_mspace);

    /* --- 6. Finalize --- */
    h->current_step++;
}

void events_vtkhdf_close(const struct vtkhdf_handle *const h)
{
    const hid_t root = H5Gopen2(h->file_id, "VTKHDF/Steps", H5P_DEFAULT);
    H5_CHECK(root);
    const hid_t s_space = H5Screate(H5S_SCALAR);
    H5_CHECK(s_space);
    const hid_t attr = H5Acreate2(root, "NSteps", H5T_NATIVE_UINT64, s_space, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(attr);
    H5Awrite(attr, H5T_NATIVE_UINT64, &h->current_step);

    H5Aclose(attr);
    H5Sclose(s_space);
    H5Gclose(root);

    H5Dclose(h->pts_dset);
    H5Dclose(h->status_dset);
    H5Dclose(h->step_vals_dset);
    H5Dclose(h->step_pts_off_dset);
    H5Dclose(h->awacs_pos_dset);
    H5Dclose(h->awacs_dir_dset);
    H5Dclose(h->sensor_dir_dset);

    H5Fclose(h->file_id);
}

void events_write_xdmf(const char *xmf_filename, const char *h5_filename,
                       const unsigned long num_steps, const unsigned num_targets)
{
    FILE *fp = fopen(xmf_filename, "w");
    if (!fp) return;

    fprintf(fp, "<?xml version=\"1.0\" ?>\n");
    fprintf(fp, "<!DOCTYPE Xdmf SYSTEM \"Xdmf.dtd\" []>\n");
    fprintf(fp, "<Xdmf Version=\"3.0\">\n");
    fprintf(fp, "  <Domain>\n");

    /* --- COLLECTION 1: TARGETS --- */
    fprintf(fp, "    <Grid Name=\"TargetSeries\" GridType=\"Collection\" CollectionType=\"Temporal\">\n");
    for (unsigned long step = 0; step < num_steps; step++) {
        fprintf(fp, "      <Grid Name=\"T_Step_%lu\" GridType=\"Uniform\">\n", step);
        fprintf(fp, "        <Time Value=\"%f\" />\n", (float)step * time_step);
        fprintf(fp, "        <Topology TopologyType=\"Polyvertex\" NumberOfElements=\"%u\"/>\n", num_targets);
        fprintf(fp, "        <Geometry GeometryType=\"XYZ\">\n");
        fprintf(fp, "          <DataItem ItemType=\"HyperSlab\" Dimensions=\"%u 3\" Type=\"HyperSlab\">\n", num_targets);
        fprintf(fp, "            <DataItem Dimensions=\"3 2\" Format=\"XML\"> %lu 0 1 1 %u 3 </DataItem>\n", (unsigned long)step * num_targets, num_targets);
        fprintf(fp, "            <DataItem Format=\"HDF\" NumberType=\"Float\" Precision=\"4\" Dimensions=\"%lu 3\">%s:/VTKHDF/Points</DataItem>\n", (unsigned long)num_steps * num_targets, h5_filename);
        fprintf(fp, "          </DataItem>\n");
        fprintf(fp, "        </Geometry>\n");

        fprintf(fp, "          <Attribute Name=\"DetectStatus\" AttributeType=\"Scalar\" Center=\"Node\">\n");
        fprintf(fp, "            <DataItem ItemType=\"HyperSlab\" Dimensions=\"%u\" Type=\"HyperSlab\">\n", num_targets);
        fprintf(fp, "              <DataItem Dimensions=\"3 1\" Format=\"XML\"> %lu 1 %u </DataItem>\n",
                step * (unsigned long)num_targets, num_targets);
        fprintf(fp, "              <DataItem Format=\"HDF\" NumberType=\"Float\" Precision=\"4\" Dimensions=\"%lu\">%s:/VTKHDF/PointData/DetectStatus</DataItem>\n",
                num_steps * (unsigned long)num_targets, h5_filename);
        fprintf(fp, "            </DataItem>\n");
        fprintf(fp, "          </Attribute>\n");
        fprintf(fp, "        </Grid>\n");
    }
    fprintf(fp, "    </Grid>\n");

/* --- COLLECTION 2: AWACS --- */
    fprintf(fp, "    <Grid Name=\"AWACSSeries\" GridType=\"Collection\" CollectionType=\"Temporal\">\n");
    for (unsigned long step = 0; step < num_steps; step++) {
        fprintf(fp, "      <Grid Name=\"A_Step_%lu\" GridType=\"Uniform\">\n", step);
        fprintf(fp, "        <Time Value=\"%f\" />\n", (float)step * time_step);
        fprintf(fp, "        <Topology TopologyType=\"Polyvertex\" NumberOfElements=\"1\"/>\n");

        fprintf(fp, "        <Geometry GeometryType=\"XYZ\">\n");
        fprintf(fp, "          <DataItem ItemType=\"HyperSlab\" Dimensions=\"1 3\" Type=\"HyperSlab\">\n");
        fprintf(fp, "            <DataItem Dimensions=\"3 2\" Format=\"XML\"> %lu 0 1 1 1 3 </DataItem>\n", step);
        fprintf(fp, "            <DataItem Format=\"HDF\" NumberType=\"Float\" Precision=\"4\" Dimensions=\"%lu 3\">%s:/VTKHDF/FieldData/AWACS_Position</DataItem>\n", num_steps, h5_filename);
        fprintf(fp, "          </DataItem>\n");
        fprintf(fp, "        </Geometry>\n");

        /* AWACS Direction Vector */
        fprintf(fp, "        <Attribute Name=\"AWACS_Direction\" AttributeType=\"Vector\" Center=\"Node\">\n");
        fprintf(fp, "          <DataItem ItemType=\"HyperSlab\" Dimensions=\"1 3\" Type=\"HyperSlab\">\n");
        fprintf(fp, "            <DataItem Dimensions=\"3 2\" Format=\"XML\"> %lu 0  1 1  1 3 </DataItem>\n", step);
        fprintf(fp, "            <DataItem Format=\"HDF\" NumberType=\"Float\" Precision=\"4\" Dimensions=\"%lu 3\">%s:/VTKHDF/FieldData/AWACS_Direction</DataItem>\n", num_steps, h5_filename);
        fprintf(fp, "          </DataItem>\n");
        fprintf(fp, "        </Attribute>\n");

        /* Sensor Direction Vector */
        fprintf(fp, "        <Attribute Name=\"Sensor_Direction\" AttributeType=\"Vector\" Center=\"Node\">\n");
        fprintf(fp, "          <DataItem ItemType=\"HyperSlab\" Dimensions=\"1 3\" Type=\"HyperSlab\">\n");
        fprintf(fp, "            <DataItem Dimensions=\"3 2\" Format=\"XML\"> %lu 0  1 1  1 3 </DataItem>\n", step);
        fprintf(fp, "            <DataItem Format=\"HDF\" NumberType=\"Float\" Precision=\"4\" Dimensions=\"%lu 3\">%s:/VTKHDF/FieldData/Sensor_Direction</DataItem>\n", num_steps, h5_filename);
        fprintf(fp, "          </DataItem>\n");
        fprintf(fp, "        </Attribute>\n");

        fprintf(fp, "      </Grid>\n");
    }
    fprintf(fp, "    </Grid>\n");

    fprintf(fp, "  </Domain>\n");
    fprintf(fp, "</Xdmf>\n");
    fclose(fp);
}

/* Export the racetrack orbit as a 3D line loop for ParaView (.vtp) */
void racetrack_write_vtp(const struct racetrack *rt, const char *filename)
{
    const unsigned int npoints = 500u;

    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for writing.\n", filename);
        return;
    }

    fprintf(fp, "<?xml version=\"1.0\"?>\n");
    fprintf(fp, "<VTKFile type=\"PolyData\" version=\"0.1\" byte_order=\"LittleEndian\">\n");
    fprintf(fp, "  <PolyData>\n");
    fprintf(fp, "    <Piece NumberOfPoints=\"%d\" NumberOfLines=\"1\">\n", npoints);
    fprintf(fp, "      <Points>\n");
    fprintf(fp, "        <DataArray type=\"Float32\" NumberOfComponents=\"3\" format=\"ascii\">\n");

    struct platform_state st;
    const float tstep = rt->orbit_time_s / (float)npoints;

    for (unsigned int i = 0; i < npoints; i++) {
        const float t = rt->start_time + ((float)i * tstep);
        platform_state_update(&st, rt, t);
        fprintf(fp, "          %.2f %.2f %.2f\n", st.x, st.y, st.alt);
    }

    fprintf(fp, "        </DataArray>\n");
    fprintf(fp, "      </Points>\n");
    fprintf(fp, "      <Lines>\n");
    fprintf(fp, "        <DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n");
    fprintf(fp, "          ");
    for (unsigned int i = 0; i < npoints; i++) {
        fprintf(fp, "%d ", i);
    }

    fprintf(fp, "0\n");
    fprintf(fp, "        </DataArray>\n");
    fprintf(fp, "        <DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n");
    fprintf(fp, "          %d\n", npoints + 1);
    fprintf(fp, "        </DataArray>\n");
    fprintf(fp, "      </Lines>\n");
    fprintf(fp, "      <PointData>\n");
    fprintf(fp, "        <DataArray type=\"Float32\" Name=\"Roll_Angle\" format=\"ascii\">\n");
    for (unsigned int i = 0; i < npoints; i++) {
        /* Abusing the platform state calculator to get point coordinates */
        platform_state_update(&st, rt, rt->start_time + (float)i * tstep);
        fprintf(fp, "          %.4f\n", st.rol * (180.0 / M_PI));
    }

    fprintf(fp, "        </DataArray>\n");
    fprintf(fp, "      </PointData>\n");
    fprintf(fp, "    </Piece>\n");
    fprintf(fp, "  </PolyData>\n");
    fprintf(fp, "</VTKFile>\n");

    fclose(fp);
}
