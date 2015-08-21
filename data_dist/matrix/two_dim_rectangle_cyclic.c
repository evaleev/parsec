/*
 * Copyright (c) 2009-2015 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 */

#include "dague/dague_internal.h"
#include "dague/debug.h"
#include "data_dist/matrix/matrix.h"
#include "data_dist/matrix/two_dim_rectangle_cyclic.h"
#include "dague/devices/device.h"
#include "dague/vpmap.h"

#ifdef HAVE_MPI
#include <mpi.h>
#endif /* HAVE_MPI */

static uint32_t twoDBC_rank_of(dague_ddesc_t* ddesc, ...);
static int32_t twoDBC_vpid_of(dague_ddesc_t* ddesc, ...);
static dague_data_t* twoDBC_data_of(dague_ddesc_t* ddesc, ...);
static uint32_t twoDBC_rank_of_key(dague_ddesc_t* ddesc, dague_data_key_t key);
static int32_t twoDBC_vpid_of_key(dague_ddesc_t* ddesc, dague_data_key_t key);
static dague_data_t* twoDBC_data_of_key(dague_ddesc_t* ddesc, dague_data_key_t key);

static uint32_t twoDBC_stview_rank_of(dague_ddesc_t* ddesc, ...);
static int32_t twoDBC_stview_vpid_of(dague_ddesc_t* ddesc, ...);
static dague_data_t* twoDBC_stview_data_of(dague_ddesc_t* ddesc, ...);
static uint32_t twoDBC_stview_rank_of_key(dague_ddesc_t* ddesc, dague_data_key_t key);
static int32_t twoDBC_stview_vpid_of_key(dague_ddesc_t* ddesc, dague_data_key_t key);
static dague_data_t* twoDBC_stview_data_of_key(dague_ddesc_t* ddesc, dague_data_key_t key);

#if defined(DAGUE_HARD_SUPERTILE)
static uint32_t twoDBC_st_rank_of(dague_ddesc_t* ddesc, ...);
static int32_t twoDBC_st_vpid_of(dague_ddesc_t* ddesc, ...);
static dague_data_t* twoDBC_st_data_of(dague_ddesc_t* ddesc, ...);
static uint32_t twoDBC_st_rank_of_key(dague_ddesc_t* ddesc, dague_data_key_t key);
static int32_t twoDBC_st_vpid_of_key(dague_ddesc_t* ddesc, dague_data_key_t key);
static dague_data_t* twoDBC_st_data_of_key(dague_ddesc_t* ddesc, dague_data_key_t key);
#endif

static int twoDBC_memory_register(dague_ddesc_t* desc, struct dague_device_s* device)
{
    two_dim_block_cyclic_t * twodbc = (two_dim_block_cyclic_t *)desc;
    return device->device_memory_register(device, desc,
                                          twodbc->mat,
                                          ((size_t)twodbc->super.nb_local_tiles * (size_t)twodbc->super.bsiz *
                                           (size_t)dague_datadist_getsizeoftype(twodbc->super.mtype)));
}

static int twoDBC_memory_unregister(dague_ddesc_t* desc, struct dague_device_s* device)
{
    two_dim_block_cyclic_t * twodbc = (two_dim_block_cyclic_t *)desc;
    return device->device_memory_unregister(device, desc, twodbc->mat);
}

void two_dim_block_cyclic_init(two_dim_block_cyclic_t * Ddesc,
                               enum matrix_type mtype,
                               enum matrix_storage storage,
                               int nodes, int myrank,
                               int mb,   int nb,   /* Tile size */
                               int lm,   int ln,   /* Global matrix size (what is stored)*/
                               int i,    int j,    /* Staring point in the global matrix */
                               int m,    int n,    /* Submatrix size (the one concerned by the computation */
                               int nrst, int ncst, /* Super-tiling size */
                               int P )
{
    int temp, Q;
    dague_ddesc_t *o = &(Ddesc->super.super);
    tiled_matrix_desc_t *tdesc = &(Ddesc->super);

    /* Initialize the tiled_matrix descriptor */
    tiled_matrix_desc_init( &(Ddesc->super), mtype, storage, two_dim_block_cyclic_type,
                            nodes, myrank,
                            mb, nb, lm, ln, i, j, m, n );
    Ddesc->mat = NULL;  /* No data associated with the matrix yet */

    /* WARNING: This has to be removed when padding will be removed */
#if defined(HAVE_MPI)
    if ( (storage == matrix_Lapack) && (P > 1)) {
        if ( tdesc->lm % mb != 0 ) {
            fprintf(stderr, "In distributed with Lapack storage, lm has to be a multiple of mb\n");
            MPI_Abort(MPI_COMM_WORLD, 2);
        }
        if ( tdesc->ln % nb != 0 ) {
            fprintf(stderr, "In distributed with Lapack storage, ln has to be a multiple of nb\n");
            MPI_Abort(MPI_COMM_WORLD, 2);
        }
    }
#endif

    if(nodes < P)
        ERROR(("Block Cyclic Distribution:\tThere are not enough nodes (%d) to make a process grid with P=%d\n", nodes, P));
    Q = nodes / P;
    if(nodes != P*Q)
        WARNING(("Block Cyclic Distribution:\tNumber of nodes %d doesn't match the process grid %dx%d\n", nodes, P, Q));

    assert( (storage != matrix_Lapack) || (P==1) );

#if defined(DAGUE_HARD_SUPERTILE)
    grid_2Dcyclic_init(&Ddesc->grid, myrank, P, Q, nrst, ncst);
#else
    grid_2Dcyclic_init(&Ddesc->grid, myrank, P, Q, 1, 1);
#endif /* DAGUE_HARD_SUPERTILE */

    /* Compute the number of rows handled by the local process */
    Ddesc->nb_elem_r = 0;
    temp = Ddesc->grid.rrank * Ddesc->grid.strows; /* row coordinate of the first tile to handle */
    while( temp < Ddesc->super.lmt ) {
        if( (temp + (Ddesc->grid.strows)) < Ddesc->super.lmt ) {
            Ddesc->nb_elem_r += (Ddesc->grid.strows);
            temp += ((Ddesc->grid.rows) * (Ddesc->grid.strows));
            continue;
        }
        Ddesc->nb_elem_r += ((Ddesc->super.lmt) - temp);
        break;
    }

    /* Compute the number of columns handled by the local process */
    Ddesc->nb_elem_c = 0;
    temp = Ddesc->grid.crank * Ddesc->grid.stcols;
    while( temp < Ddesc->super.lnt ) {
        if( (temp + (Ddesc->grid.stcols)) < Ddesc->super.lnt ) {
            Ddesc->nb_elem_c += (Ddesc->grid.stcols);
            temp += (Ddesc->grid.cols) * (Ddesc->grid.stcols);
            continue;
        }
        Ddesc->nb_elem_c += ((Ddesc->super.lnt) - temp);
        break;
    }

    /* Total number of tiles stored locally */
    Ddesc->super.nb_local_tiles = Ddesc->nb_elem_r * Ddesc->nb_elem_c;
    Ddesc->super.data_map = (dague_data_t**)calloc(Ddesc->super.nb_local_tiles, sizeof(dague_data_t*));

    /* Update llm and lln */
    Ddesc->super.llm = Ddesc->nb_elem_r * mb;
    Ddesc->super.lln = Ddesc->nb_elem_c * nb;

    /* set the methods */
    if( (nrst == 1) && (ncst == 1) ) {
        o->rank_of      = twoDBC_rank_of;
        o->vpid_of      = twoDBC_vpid_of;
        o->data_of      = twoDBC_data_of;
        o->rank_of_key  = twoDBC_rank_of_key;
        o->vpid_of_key  = twoDBC_vpid_of_key;
        o->data_of_key  = twoDBC_data_of_key;
    } else {
#if defined(DAGUE_HARD_SUPERTILE)
        o->rank_of      = twoDBC_st_rank_of;
        o->vpid_of      = twoDBC_st_vpid_of;
        o->data_of      = twoDBC_st_data_of;
        o->rank_of_key  = twoDBC_st_rank_of_key;
        o->vpid_of_key  = twoDBC_st_vpid_of_key;
        o->data_of_key  = twoDBC_st_data_of_key;
#else
        two_dim_block_cyclic_supertiled_view(Ddesc, Ddesc, nrst, ncst);
#endif /* DAGUE_HARD_SUPERTILE */
    }
    o->register_memory   = twoDBC_memory_register;
    o->unregister_memory = twoDBC_memory_unregister;

    DEBUG3(("two_dim_block_cyclic_init: \n"
           "      Ddesc = %p, mtype = %d, nodes = %u, myrank = %d, \n"
           "      mb = %d, nb = %d, lm = %d, ln = %d, i = %d, j = %d, m = %d, n = %d, \n"
           "      nrst = %d, ncst = %d, P = %d, Q = %d\n",
           Ddesc, Ddesc->super.mtype, Ddesc->super.super.nodes,
           Ddesc->super.super.myrank,
           Ddesc->super.mb, Ddesc->super.nb,
           Ddesc->super.lm, Ddesc->super.ln,
           Ddesc->super.i,  Ddesc->super.j,
           Ddesc->super.m,  Ddesc->super.n,
           Ddesc->grid.strows, Ddesc->grid.stcols,
           P, Q));
}

static void twoDBC_key_to_coordinates(dague_ddesc_t *desc, dague_data_key_t key, int *m, int *n)
{
    int _m, _n;
    tiled_matrix_desc_t * Ddesc;

    Ddesc = (tiled_matrix_desc_t *)desc;

    _m = key % Ddesc->lmt;
    _n = key / Ddesc->lmt;
    *m = _m - Ddesc->i / Ddesc->mb;
    *n = _n - Ddesc->j / Ddesc->nb;
}

/*
 *
 * Set of functions with no super-tiles
 *
 */
static uint32_t twoDBC_rank_of(dague_ddesc_t * desc, ...)
{
    int cr, m, n;
    int rr;
    int res;
    va_list ap;
    two_dim_block_cyclic_t * Ddesc = (two_dim_block_cyclic_t *)desc;

    /* Get coordinates */
    va_start(ap, desc);
    m = va_arg(ap, unsigned int);
    n = va_arg(ap, unsigned int);
    va_end(ap);

    /* Offset by (i,j) to translate (m,n) in the global matrix */
    m += Ddesc->super.i / Ddesc->super.mb;
    n += Ddesc->super.j / Ddesc->super.nb;

    assert( m < Ddesc->super.mt );
    assert( n < Ddesc->super.nt );

    /* P(rr, cr) has the tile, compute the mpi rank*/
    rr = m % Ddesc->grid.rows;
    cr = n % Ddesc->grid.cols;
    res = rr * Ddesc->grid.cols + cr;

    return res;
}

static uint32_t twoDBC_rank_of_key(dague_ddesc_t *desc, dague_data_key_t key)
{
    int m, n;
    twoDBC_key_to_coordinates(desc, key, &m, &n);
    return twoDBC_rank_of(desc, m, n);
}

static int32_t twoDBC_vpid_of(dague_ddesc_t *desc, ...)
{
    int m, n, p, q, pq;
    int local_m, local_n;
    two_dim_block_cyclic_t * Ddesc;
    va_list ap;
    int32_t vpid;
    Ddesc = (two_dim_block_cyclic_t *)desc;

    /* If 1 VP, always return 0 */
    pq = vpmap_get_nb_vp();
    if ( pq == 1 )
        return 0;

    q = Ddesc->grid.vp_q;
    p = Ddesc->grid.vp_p;
    assert(p*q == pq);

    /* Get coordinates */
    va_start(ap, desc);
    m = (int)va_arg(ap, unsigned int);
    n = (int)va_arg(ap, unsigned int);
    va_end(ap);

    /* Offset by (i,j) to translate (m,n) in the global matrix */
    m += Ddesc->super.i / Ddesc->super.mb;
    n += Ddesc->super.j / Ddesc->super.nb;

    assert( m < Ddesc->super.mt );
    assert( n < Ddesc->super.nt );

#if defined(DISTRIBUTED)
    assert(desc->myrank == twoDBC_rank_of(desc, m, n));
#endif

    /* Compute the local tile row */
    local_m = m / Ddesc->grid.rows;
    assert( (m % Ddesc->grid.rows) == Ddesc->grid.rrank );

    /* Compute the local column */
    local_n = n / Ddesc->grid.cols;
    assert( (n % Ddesc->grid.cols) == Ddesc->grid.crank );

    vpid = (local_n % q) * p + (local_m % p);
    assert( vpid < vpmap_get_nb_vp() );
    return vpid;
}

static int32_t twoDBC_vpid_of_key(dague_ddesc_t *desc, dague_data_key_t key)
{
    int m, n;
    twoDBC_key_to_coordinates(desc, key, &m, &n);
    return twoDBC_vpid_of(desc, m, n);
}

/*
 * Do not change this function without updating the inverse function:
 * twoDBC_position_to_coordinates()
 * Other files (zhebut) depend on the inverse function.
 */
inline int twoDBC_coordinates_to_position(two_dim_block_cyclic_t *Ddesc, int m, int n){
    int position, local_m, local_n;

    /* Compute the local tile row */
    local_m = m / Ddesc->grid.rows;
    assert( (m % Ddesc->grid.rows) == Ddesc->grid.rrank );

    /* Compute the local column */
    local_n = n / Ddesc->grid.cols;
    assert( (n % Ddesc->grid.cols) == Ddesc->grid.crank );

    assert(Ddesc->nb_elem_r <= Ddesc->super.lmt);
    position = Ddesc->nb_elem_r * local_n + local_m;

    return position;
}

/*
 * This is the inverse function of: twoDBC_coordinates_to_position()
 * Please keep them in sync, other files (zhebut) depend on this function.
 */
inline void twoDBC_position_to_coordinates(two_dim_block_cyclic_t *Ddesc, int position, int *m, int *n)
{
    int local_m, local_n;

    local_m = position%(Ddesc->nb_elem_r);
    local_n = position/(Ddesc->nb_elem_r);

    *m = local_m*(Ddesc->grid.rows) + Ddesc->grid.rrank;
    *n = local_n*(Ddesc->grid.cols) + Ddesc->grid.crank;
#if defined(DAGUE_DEBUG_ENABLE)
    assert(position == twoDBC_coordinates_to_position(Ddesc, *m, *n));
#endif  /* defined(DAGUE_DEBUG_ENABLE) */

    return;
}

static dague_data_t* twoDBC_data_of(dague_ddesc_t *desc, ...)
{
    int m, n, position;
    size_t pos;
    va_list ap;
    two_dim_block_cyclic_t * Ddesc;
    Ddesc = (two_dim_block_cyclic_t *)desc;

    /* Get coordinates */
    va_start(ap, desc);
    m = (int)va_arg(ap, unsigned int);
    n = (int)va_arg(ap, unsigned int);
    va_end(ap);

    /* Offset by (i,j) to translate (m,n) in the global matrix */
    m += Ddesc->super.i / Ddesc->super.mb;
    n += Ddesc->super.j / Ddesc->super.nb;

    assert( m < Ddesc->super.mt );
    assert( n < Ddesc->super.nt );

#if defined(DISTRIBUTED)
    assert(desc->myrank == twoDBC_rank_of(desc, m, n));
#endif

    position = twoDBC_coordinates_to_position(Ddesc, m, n);

    if( Ddesc->super.storage == matrix_Tile ) {
        pos = position;
        pos *= (size_t)Ddesc->super.bsiz;
    } else {
        int local_m = m / Ddesc->grid.rows;
        int local_n = n / Ddesc->grid.cols;
        pos = (local_n * Ddesc->super.nb) * Ddesc->super.llm
            +  local_m * Ddesc->super.mb;
    }

    return dague_matrix_create_data( &Ddesc->super,
                                     (char*)Ddesc->mat + pos * dague_datadist_getsizeoftype(Ddesc->super.mtype),
                                     position, (n * Ddesc->super.lmt) + m );
}

static dague_data_t* twoDBC_data_of_key(dague_ddesc_t *desc, dague_data_key_t key)
{
    int m, n;
    twoDBC_key_to_coordinates(desc, key, &m, &n);
    return twoDBC_data_of(desc, m, n);
}

/****
 * Set of functions with Supertiled view of the distribution
 ****/

void two_dim_block_cyclic_supertiled_view( two_dim_block_cyclic_t* target,
                                           two_dim_block_cyclic_t* origin,
                                           int rst, int cst )
{
    assert( (origin->grid.strows == 1) && (origin->grid.stcols == 1) );
    *target = *origin;
    target->grid.strows = rst;
    target->grid.stcols = cst;
    target->super.super.rank_of     = twoDBC_stview_rank_of;
    target->super.super.data_of     = twoDBC_stview_data_of;
    target->super.super.vpid_of     = twoDBC_stview_vpid_of;
    target->super.super.rank_of_key = twoDBC_stview_rank_of_key;
    target->super.super.data_of_key = twoDBC_stview_data_of_key;
    target->super.super.vpid_of_key = twoDBC_stview_vpid_of_key;
}

static inline unsigned int st_compute_m(two_dim_block_cyclic_t* desc, unsigned int m)
{
    unsigned int p, ps, mt;
    p = desc->grid.rows;
    ps = desc->grid.strows;
    mt = desc->super.mt;
    do {
        m = m-m%(p*ps) + (m%ps)*p + (m/ps)%p;
    } while(m >= mt);
    return m;
}

static inline unsigned int st_compute_n(two_dim_block_cyclic_t* desc, unsigned int n)
{
    unsigned int q, qs, nt;
    q = desc->grid.cols;
    qs = desc->grid.stcols;
    nt = desc->super.nt;
    do {
        n = n-n%(q*qs) + (n%qs)*q + (n/qs)%q;
    } while(n >= nt);
    return n;
}

static uint32_t twoDBC_stview_rank_of(dague_ddesc_t* ddesc, ...)
{
    unsigned int m, n, sm, sn;
    two_dim_block_cyclic_t* desc = (two_dim_block_cyclic_t*)ddesc;
    va_list ap;
    va_start(ap, ddesc);
    m = va_arg(ap, unsigned int);
    n = va_arg(ap, unsigned int);
    va_end(ap);
    sm = st_compute_m(desc, m);
    sn = st_compute_n(desc, n);
    return twoDBC_rank_of(ddesc, sm, sn);
}

static uint32_t twoDBC_stview_rank_of_key(dague_ddesc_t *desc, dague_data_key_t key)
{
    int m, n;
    twoDBC_key_to_coordinates(desc, key, &m, &n);
    return twoDBC_stview_rank_of(desc, m, n);
}

static int32_t twoDBC_stview_vpid_of(dague_ddesc_t* ddesc, ...)
{
    unsigned int m, n;
    two_dim_block_cyclic_t* desc = (two_dim_block_cyclic_t*)ddesc;
    va_list ap;
    va_start(ap, ddesc);
    m = va_arg(ap, unsigned int);
    n = va_arg(ap, unsigned int);
    va_end(ap);
    m = st_compute_m(desc, m);
    n = st_compute_n(desc, n);
    return twoDBC_vpid_of(ddesc, m, n);
}

static int32_t twoDBC_stview_vpid_of_key(dague_ddesc_t *desc, dague_data_key_t key)
{
    int m, n;
    twoDBC_key_to_coordinates(desc, key, &m, &n);
    return twoDBC_stview_vpid_of(desc, m, n);
}

static dague_data_t* twoDBC_stview_data_of(dague_ddesc_t* ddesc, ...)
{
    unsigned int m, n;
    two_dim_block_cyclic_t* desc = (two_dim_block_cyclic_t*)ddesc;
    va_list ap;
    va_start(ap, ddesc);
    m = va_arg(ap, unsigned int);
    n = va_arg(ap, unsigned int);
    va_end(ap);
    m = st_compute_m(desc, m);
    n = st_compute_n(desc, n);
    return twoDBC_data_of(ddesc, m, n);
}

static dague_data_t* twoDBC_stview_data_of_key(dague_ddesc_t *desc, dague_data_key_t key)
{
    int m, n;
    twoDBC_key_to_coordinates(desc, key, &m, &n);
    return twoDBC_stview_data_of(desc, m, n);
}

#if defined(DAGUE_HARD_SUPERTILE)
/*
 *
 * Set of functions with super-tiles
 *
 */
static uint32_t twoDBC_st_rank_of(dague_ddesc_t * desc, ...)
{
    unsigned int stc, cr, m, n;
    unsigned int str, rr;
    unsigned int res;
    va_list ap;
    two_dim_block_cyclic_t * Ddesc;
    Ddesc = (two_dim_block_cyclic_t *)desc;

    /* Get coordinates */
    va_start(ap, desc);
    m = va_arg(ap, unsigned int);
    n = va_arg(ap, unsigned int);
    va_end(ap);

    /* Offset by (i,j) to translate (m,n) in the global matrix */
    m += Ddesc->super.i / Ddesc->super.mb;
    n += Ddesc->super.j / Ddesc->super.nb;

    /* for tile (m,n), first find coordinate of process in
       process grid which possess the tile in block cyclic dist */
    /* (m,n) is in super-tile (str, stc)*/
    str = m / Ddesc->grid.strows;
    stc = n / Ddesc->grid.stcols;

    /* P(rr, cr) has the tile, compute the mpi rank*/
    rr = str % Ddesc->grid.rows;
    cr = stc % Ddesc->grid.cols;
    res = rr * Ddesc->grid.cols + cr;

    /* printf("tile (%d, %d) belongs to process %d [%d,%d] in a grid of %dx%d\n", */
    /*            m, n, res, rr, cr, Ddesc->grid.rows, Ddesc->grid.cols); */
    return res;
}

static uint32_t twoDBC_st_rank_of_key(dague_ddesc_t *desc, dague_data_key_t key)
{
    int m, n;
    twoDBC_key_to_coordinates(desc, key, &m, &n);
    return twoDBC_st_rank_of(desc, m, n);
}

static int32_t twoDBC_st_vpid_of(dague_ddesc_t *desc, ...)
{
    int m, n, p, q, pq;
    int local_m, local_n;
    two_dim_block_cyclic_t * Ddesc;
    va_list ap;
    int32_t vpid;
    Ddesc = (two_dim_block_cyclic_t *)desc;

    /* If no vp, always return 0 */
    pq = vpmap_get_nb_vp();
    if ( pq == 1 )
        return 0;

    q = Ddesc->grid.vp_q;
    p = Ddesc->grid.vp_p;
    assert(p*q == pq);

    /* Get coordinates */
    va_start(ap, desc);
    m = (int)va_arg(ap, unsigned int);
    n = (int)va_arg(ap, unsigned int);
    va_end(ap);

    /* Offset by (i,j) to translate (m,n) in the global matrix */
    m += Ddesc->super.i / Ddesc->super.mb;
    n += Ddesc->super.j / Ddesc->super.nb;

#if defined(DISTRIBUTED)
    assert(desc->myrank == twoDBC_st_rank_of(desc, m, n));
#endif

    /* Compute the local tile row */
    local_m = ( m / (Ddesc->grid.strows * Ddesc->grid.rows) ) * Ddesc->grid.strows;
    m = m % (Ddesc->grid.strows * Ddesc->grid.rows);
    assert( m / Ddesc->grid.strows == Ddesc->grid.rrank);
    local_m += m % Ddesc->grid.strows;

    /* Compute the local column */
    local_n = ( n / (Ddesc->grid.stcols * Ddesc->grid.cols) ) * Ddesc->grid.stcols;
    n = n % (Ddesc->grid.stcols * Ddesc->grid.cols);
    assert( n / Ddesc->grid.stcols == Ddesc->grid.crank);
    local_n += n % Ddesc->grid.stcols;

    vpid = (local_n % q) * p + (local_m % p);
    assert( vpid < vpmap_get_nb_vp() );
    return vpid;
}

static int32_t twoDBC_st_vpid_of_key(dague_ddesc_t *desc, dague_data_key_t key)
{
    int m, n;
    twoDBC_key_to_coordinates(desc, key, &m, &n);
    return twoDBC_st_vpid_of(desc, m, n);
}

static dague_data_t* twoDBC_st_data_of(dague_ddesc_t *desc, ...)
{
    size_t pos;
    int m, n, local_m, local_n, position;
    va_list ap;
    two_dim_block_cyclic_t * Ddesc;
    Ddesc = (two_dim_block_cyclic_t *)desc;

    /* Get coordinates */
    va_start(ap, desc);
    m = (int)va_arg(ap, unsigned int);
    n = (int)va_arg(ap, unsigned int);
    va_end(ap);

    /* Offset by (i,j) to translate (m,n) in the global matrix */
    m += Ddesc->super.i / Ddesc->super.mb;
    n += Ddesc->super.j / Ddesc->super.nb;

#if defined(DISTRIBUTED)
    assert(desc->myrank == twoDBC_st_rank_of(desc, m, n));
#endif

    /* Compute the local tile row */
    local_m = ( m / (Ddesc->grid.strows * Ddesc->grid.rows) ) * Ddesc->grid.strows;
    m = m % (Ddesc->grid.strows * Ddesc->grid.rows);
    assert( m / Ddesc->grid.strows == Ddesc->grid.rrank);
    local_m += m % Ddesc->grid.strows;

    /* Compute the local column */
    local_n = ( n / (Ddesc->grid.stcols * Ddesc->grid.cols) ) * Ddesc->grid.stcols;
    n = n % (Ddesc->grid.stcols * Ddesc->grid.cols);
    assert( n / Ddesc->grid.stcols == Ddesc->grid.crank);
    local_n += n % Ddesc->grid.stcols;

    position = Ddesc->nb_elem_r * local_n + local_m;;
    if( Ddesc->super.storage == matrix_Tile ) {
        pos = position;
        pos *= (size_t)Ddesc->super.bsiz;
    } else {
        pos = (local_n * Ddesc->super.nb) * Ddesc->super.llm
            +  local_m * Ddesc->super.mb;
    }

    return dague_matrix_create_data( &Ddesc->super,
                                     (char*)Ddesc->mat + pos * dague_datadist_getsizeoftype(Ddesc->super.mtype),
                                     position, (n * Ddesc->super.lmt) + m );
}

static dague_data_t* twoDBC_st_data_of_key(dague_ddesc_t *desc, dague_data_key_t key)
{
    int m, n;
    twoDBC_key_to_coordinates(desc, key, &m, &n);
    return twoDBC_st_data_of(desc, m, n);
}

#endif /* DAGUE_HARD_SUPERTILE */
