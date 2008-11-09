#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "IBLSampler.h"

#include "ray.h"
#include "reflection.h"
#include "random.h"
#include "raytrace.h"
#include "image_saver.h"
#include "beam.h"

#define RESOLUTION  (64)
#define SDIV        (2)

#define MIN_DIV_SIZE (M_PI / (RESOLUTION))

//
// TODO: Use SAT technique.
//

static void uv2xyz(
    double *x,
    double *y,
    double *z,
    double  u,                  /* [0, pi]      */
    double  v)                  /* [0, 2 pi]    */
{
    //
    // The scene is defined in +Y up coord, so we align to this coord.
    // It differs standard uv2xyz definition which is defined in +Z up coord.)
    //
    //(*x) = cos(v) * sin(u);
    //(*y) = sin(v) * sin(u);
    //(*z) = cos(u);
    (*z) = cos(v) * sin(u);     // x
    (*x) = sin(v) * sin(u);     // y
    (*y) = cos(u);              // z
}

static int
check_visibility(
    ri_bvh_t *bvh,
    vec       org,
    vec       dir[4])
{
    ri_beam_t beam;
    int invalid;
    int vis;

    invalid = ri_beam_set( &beam, org, dir );
    if (invalid) exit(0);
    assert(invalid == 0);

    vis  = ri_bvh_intersect_beam_visibility(
                (void *)bvh, &beam, NULL);

    return vis;

}


static void
contribute(
    float          *dst,
    float          *L,                  /* IBL image in long-lat coord */
    ri_vector_t     n,
    double          theta,
    double          phi,
    double          theta_step,
    double          phi_step,
    int             width,
    int             height)
{

    int u, v;
    int us, ue;
    int vs, ve;
    int idx;

    double u_step, v_step;
    double t, p; 
    double dir[3];
    double cosTheta;

    us = (int)((phi / (2.0 * M_PI)) * width);
    if (us >= width) us = width - 1;
    ue = (int)(((phi + phi_step) / (2.0 * M_PI)) * width);
    if (ue >= width) ue = width - 1;
    u_step = phi_step / (ue - us);

    vs = (int)((theta / M_PI) * height);
    if (vs >= height) vs = height - 1;
    ve = (int)(((theta + theta_step) / M_PI) * height);
    if (ve >= height) ve = height - 1;
    v_step = theta_step / (ve - vs);

    // TODO: employ fast spherical interpolation?

    for (v = vs; v < ve; v++) {

        t = theta + (v - vs) * v_step; 

        for (u = us; u < ue; u++) {

            p = phi + (u - us) * u_step; 

            uv2xyz(&dir[0], &dir[1], &dir[2], t, p);

            cosTheta = vdot(dir, n);
            if (cosTheta < 0.0) cosTheta = 0.0;

            // L x cosTheta
            idx = 4 * (v * width + u);
            dst[idx + 0] = cosTheta * L[idx + 0];
            dst[idx + 1] = cosTheta * L[idx + 1];
            dst[idx + 2] = cosTheta * L[idx + 2];
        }
    }

}


static void
render_subregion(
    ri_bvh_t                        *bvh,
    const ri_texture_t              *Lmap,
    ri_texture_t                    *prodmap,
    const ri_intersection_state_t   *isect,
    ri_vector_t                      n,
    double                           theta,
    double                           phi,
    double                           theta_step,
    double                           phi_step,
    int                              depth)
{
    vec    dir[4];
    double dot;
    double sub_theta;
    double sub_phi;
    double sub_theta_step;
    double sub_phi_step;
    double eps = 1.0e-6;
    vec    basis[3];
    

    int i, j;

    ri_ortho_basis(basis, n);

    //printf("depth %d, subregion theta = %f, phi = %f, tstep = %f, pstep = %f\n",
    //    depth, theta, phi, theta_step, phi_step);

    if ((theta_step < MIN_DIV_SIZE) ||
        (phi_step   < MIN_DIV_SIZE) ||
        (depth      > 7)) {
        return;
    }

    // Add eps to avoid numerical problem.
    uv2xyz(&dir[0][0], &dir[0][1], &dir[0][2],
           theta + eps, phi + eps);
    uv2xyz(&dir[1][0], &dir[1][1], &dir[1][2],
           theta + eps, phi + phi_step - eps);
    uv2xyz(&dir[2][0], &dir[2][1], &dir[2][2],
           theta + theta_step - eps, phi + eps);
    uv2xyz(&dir[3][0], &dir[3][1], &dir[3][2],
           theta + theta_step - eps, phi + phi_step - eps);
    

    /* Early check of visibility.
     * If all dot product of direction and normal are negative,
     * the beam is under the hemisphere.
     * In this case, there's no need to trace for checking visibility.
     */

    int nunder_hemisphere = 0;
    for (i = 0; i < 4; i++) {
        dot = vdot(dir[i], n);

        //printf("dir[%d]  = %f, %f, %f\n", i, dir[i][0], dir[i][1], dir[i][2]);
        //printf("vdot[%d] = %f\n", i, dot);

        if (dot < 0.0) nunder_hemisphere++;
    }
    

    //printf("nunder_hemisphere = %d\n", nunder_hemisphere);

    if (nunder_hemisphere == 4) {
        /* The beam is completely under the hemisphere . */
        //printf("Invisible: theta = %f, phi = %f, underhemi for normal (%f, %f, %f)\n",
        //    theta, phi, n[0], n[1], n[2]);
        return;
    }

    /* If any dot product is negative, subdivide the beam */
    if (nunder_hemisphere != 0) {
        
        for (j = 0; j < 2; j++) {           // theta
            for (i = 0; i < 2; i++) {       // phi

                /*
                 * 0 ---- 1        ----> phi
                 * |      |        |
                 * |      |        |/
                 * 2 ---- 3        theta
                 */
                sub_theta       = theta + 0.5 * j * theta_step;
                sub_phi         = phi   + 0.5 * i * phi_step;
                sub_theta_step  = 0.5 * theta_step;
                sub_phi_step    = 0.5 * phi_step;

                render_subregion(bvh, Lmap, prodmap, isect, n,
                                 sub_theta, sub_phi,
                                 sub_theta_step, sub_phi_step, depth + 1);
            }
        }

    } else {
        
        //printf("theta = %f, phi = %f, upperhemi for normal (%f, %f, %f)\n",
        //    theta, phi, n[0], n[1], n[2]);

        /*
         * Beam is upper hemisphere defined by normal 'n'.
         * Check visibility.
         */
        vec org;
        int vis;

        org[0] = isect->P[0] + 0.01 * n[0];
        org[1] = isect->P[1] + 0.01 * n[1];
        org[2] = isect->P[2] + 0.01 * n[2];

        vis = check_visibility( bvh, org, dir );

        if (vis == RI_BEAM_HIT_PARTIALLY) {

            // if (depth == 0) {
            //     printf("partially hit\n");
            //     printf("theta, phi = %f, %f\n", theta, phi);
            //     printf("dir = %f, %f, %f\n", dir[0][0], dir[0][1], dir[0][2]);
            //     printf("dir = %f, %f, %f\n", dir[1][0], dir[1][1], dir[1][2]);
            //     printf("dir = %f, %f, %f\n", dir[2][0], dir[2][1], dir[2][2]);
            //     printf("dir = %f, %f, %f\n", dir[3][0], dir[3][1], dir[3][2]);
            // }

            // Needs subdivision.

            for (j = 0; j < 2; j++) {           // theta
                for (i = 0; i < 2; i++) {       // phi

                    /*
                     * 0 ---- 1        ----> phi
                     * |      |        |
                     * |      |        |/
                     * 2 ---- 3        theta
                     */
                    sub_theta       = theta + 0.5 * j * theta_step;
                    sub_phi         = phi   + 0.5 * i * phi_step;
                    sub_theta_step  = 0.5 * theta_step;
                    sub_phi_step    = 0.5 * phi_step;

                    render_subregion(bvh, Lmap, prodmap, isect, n,
                                     sub_theta, sub_phi,
                                     sub_theta_step, sub_phi_step, depth + 1);
                }
            }

        } else if (vis == RI_BEAM_HIT_COMPLETELY) {

            // Invisible. Do nothing.

        } else if (vis == RI_BEAM_MISS_COMPLETELY) {

            // Visible all region coverd by the beam.

            contribute(
                prodmap->data,
                Lmap->data,
                n,
                theta,
                phi,
                theta_step,
                phi_step,
                prodmap->width,
                prodmap->height);
        }
    }
}

static void
convolve(
    ri_vector_t   Lo,
    ri_texture_t *prodmap)
{
    /* TODO: Use hierarchical summation technique to speed up convolution. */

    int i;
    double k = 2.0 * M_PI;
    
    vec L;
    vzero(L);
    
    for (i = 0; i < prodmap->width * prodmap->height; i++) {
        L[0] += prodmap->data[4 * i + 0];
        L[1] += prodmap->data[4 * i + 1];
        L[2] += prodmap->data[4 * i + 2];
    }

    Lo[0] = k * L[0] / (prodmap->width * prodmap->height);
    Lo[1] = k * L[1] / (prodmap->width * prodmap->height);
    Lo[2] = k * L[2] / (prodmap->width * prodmap->height);
}


/*
 * Sample IBL with beam.
 *
 * - Divide hemisphere into 16 sub-regions(4 sub-regions for each octant).
 * - Trace beam from the sub-region.
 * - calc visibility map and IBL map.
 * - calc final contribution from IBL with visibiity.
 */
void
sample_ibl_beam(
    ri_vector_t                      Lo,                /* [out]            */
    ri_bvh_t                        *bvh,
    const ri_texture_t              *Lmap,
    ri_texture_t                    *prodmap,           /* [buffer]         */
    const ri_intersection_state_t   *isect)
{

    int i;
    int o, u;
    int sx, sy;

    (void)Lmap;
    (void)isect;
    
    double theta_step = (0.5 * M_PI) / SDIV;
    double phi_step   = (2.0 * M_PI) / (4.0 * SDIV);

    double theta[4], phi[4];                // left, top, right, bottom

    for (u = 0; u < 2; u++) {               // upper and lower
        for (o = 0; o < 4; o++) {               // half of octants

            for (sy = 0; sy < SDIV; sy++) {        // sub-regions
                for (sx = 0; sx < SDIV; sx++) {

                    theta[0] = u * (0.5 * M_PI) + (sy + 0) * theta_step;
                    theta[1] = u * (0.5 * M_PI) + (sy + 0) * theta_step;
                    theta[2] = u * (0.5 * M_PI) + (sy + 1) * theta_step;
                    theta[3] = u * (0.5 * M_PI) + (sy + 1) * theta_step;

                    phi[0]   = o * (M_PI * 0.5) + (sx + 0) * phi_step;
                    phi[1]   = o * (M_PI * 0.5) + (sx + 1) * phi_step;
                    phi[2]   = o * (M_PI * 0.5) + (sx + 0) * phi_step;
                    phi[3]   = o * (M_PI * 0.5) + (sx + 1) * phi_step;

                    //for (i = 0; i < 4; i++) {
                    //    printf("[%d] = (%f, %f)\n", i, theta[i], phi[i]);
                    //}

                    render_subregion(
                        bvh,
                        Lmap,
                        prodmap,
                        isect,
                        isect->Ng,
                        theta[0], phi[0], theta_step, phi_step, 0);

                }
            }
        }
    }

    // hack
    //ri_image_save_hdr("integral.hdr", prodmap->data, prodmap->width, prodmap->height);

    convolve(Lo, prodmap);

    //printf("Lo = %f, %f, %f\n", Lo[0], Lo[1], Lo[2]);
    //exit(0);
}

/*
 * Simple MC sampling for reference.
 */
void
sample_ibl_naive(
    ri_vector_t                      Lo,                /* [out]            */
    ri_bvh_t                        *bvh,
    const ri_texture_t              *iblmap,
    const ri_intersection_state_t   *isect,
    uint32_t                         ntheta_samples,
    uint32_t                         nphi_samples)
{
    int                     k;
    int                     hit;
    uint32_t                u, v;
    ri_float_t              z0, z1;

    vec                     radiance;
    vec                     power;
    vec                     basis[3];
    vec                     dir;
    ri_ray_t                ray;
    ri_intersection_state_t state;
    ri_float_t              cosTheta, phi;

    vzero(power);

    ri_ortho_basis(basis, isect->Ng);

    ri_vector_copy(ray.org, isect->P);

    /* slightly move the shading point towards the surface normal */
    ray.org[0] += isect->Ng[0] * 0.00001;
    ray.org[1] += isect->Ng[1] * 0.00001;
    ray.org[2] += isect->Ng[2] * 0.00001;

    
    for (v = 0; v < nphi_samples; v++) {
        for (u = 0; u < ntheta_samples; u++) {

            /*
             * Do importance sampling and stratified sampling for theta and phi.
             * theta is drawn from the following probability.
             *
             *   p(x) ~ cos(theta) / PI  (against differential solid angle).
             *
             */
            z0 = ((ri_float_t)u + randomMT())/ (ri_float_t)ntheta_samples;
            z1 = ((ri_float_t)v + randomMT())/ (ri_float_t)nphi_samples;

            cosTheta = sqrt(z0);
            phi = 2.0 * M_PI * z1;
            dir[0] = cos(phi) * cosTheta;
            dir[1] = sin(phi) * cosTheta;
            dir[2] = sqrt(1.0 - cosTheta * cosTheta);

            for (k = 0; k < 3; k++) {
                ray.dir[k] = dir[0]*basis[0][k]
                           + dir[1]*basis[1][k]
                           + dir[2]*basis[2][k];
            }

            hit = ri_bvh_intersect( (void *)bvh, &ray, &state, NULL );

            if (!hit) {

                /*
                 * Contribution from IBL.
                 */
                ri_texture_ibl_fetch(radiance,
                                     iblmap,
                                     ray.dir);

                vadd(power, power, radiance);
            }
        }
    }

    // Lo = (1/pi) * Lsum / (ntheta * nphi)
    vscale(Lo, power, 1.0 / (M_PI * ntheta_samples * nphi_samples));
}
