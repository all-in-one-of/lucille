/*
 * reflection routine.
 *
 * $Id: reflection.h,v 1.5 2004/08/15 05:19:39 syoyo Exp $
 */

#ifndef REFLECTION_H
#define REFLECTION_H

#include "vector.h"

#ifdef __cplusplus
extern "C" {
#endif

/* calculate reflected vector */
extern void ri_reflect(
    ri_vector_t       refrect,          /* [out] */
    const ri_vector_t in,
    const ri_vector_t n); 

/* calculate refracted vector */
extern int  ri_refract(
    ri_vector_t       refract,          /* [out] */
    const ri_vector_t in,
    const ri_vector_t n,
    ri_float_t        eta);

/* calculate cosine weighted random vector around the vector n */
extern void ri_random_vector_cosweight(
    ri_vector_t        v,               /* [out */
    const ri_vector_t  n);

/* calculate cosine^N weighted random vector around vector n */
extern void ri_random_vector_cosNweight(
    ri_vector_t        v,               /* [out] */
    ri_float_t        *pdf,
    const ri_vector_t  n,
    const ri_float_t   u0,
    const ri_float_t   u1,
    const ri_float_t   N);

/* calculate cosine weighted quasi-random vector around normal vector */
extern void ri_qmc_vector_cosweight(
    ri_vector_t        v,               /* [out] */
    const ri_vector_t  n,
    int                d,
    int                i,
    int                **perm);

/* calculate fresnel factor. */
extern void   ri_fresnel(
    ri_vector_t        r_out,   /* reclected vector    */
    ri_vector_t        t_out,   /* transmitted vector    */
    ri_float_t        *kr,      /* the reflection coeff */
    ri_float_t        *kt,      /* the refraction coeff */
    const ri_vector_t *in,
    const ri_vector_t *n,
    const ri_float_t   eta);

/* calcualte half vector. */
extern void ri_hvector(
    ri_vector_t       h,            /* [out] */
    const ri_vector_t l,
    const ri_vector_t v);

extern void ri_ortho_basis(
    ri_vector_t       basis[3],       /* [out] */
    const ri_vector_t n);

#ifdef __cplusplus
}    /* extern "C" */
#endif

#endif
