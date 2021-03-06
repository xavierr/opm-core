/* Copyright 2011 (c) Jostein R. Natvig <Jostein.R.Natvig at sintef.no> */

#include "config.h"
#include <opm/core/grid.h>

#ifdef MATLAB_MEX_FILE
#include "reordersequence.h"
#include "tarjan.h"
#else
#include <opm/core/transport/reorder/reordersequence.h>
#include <opm/core/transport/reorder/tarjan.h>
#endif

#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

struct SortByAbsFlux
{
    SortByAbsFlux(const double* flux)
        : flux_(flux)
    {}
    bool operator() (int f1, int f2)
    {
        return std::fabs(flux_[f1]) < std::fabs(flux_[f2]);
    }
    const double* flux_;
};


/* Construct adjacency matrix of upwind graph wrt flux.  Column
   indices are not sorted. */
// ---------------------------------------------------------------------
static void
make_upwind_graph(int           nc       ,
                  const int    *cellfaces,
                  const int    *faceptr  ,
                  const int    *face2cell,
                  const double *flux     ,
                  int          *ia       ,
                  int          *ja       ,
                  int          *work     )
// ---------------------------------------------------------------------
{
    /* Using topology (conn, cptr), and direction, construct adjacency
       matrix of graph. */

    int i, j, p, f, positive_sign, boundaryface;
    double theflux;

    /* For each face, store upwind cell in work array */
    for (i=0; i<nc; ++i)
    {
        for (j=faceptr[i]; j<faceptr[i+1]; ++j)
        {
            f  = cellfaces[j];
            positive_sign = (i == face2cell[2*f]);
            theflux = positive_sign ? flux[f] : -flux[f];

            if ( theflux > 0  )
            {
                /* i is upwind cell for face f */
                work[f] = i;
            }
        }
    }

    /* Fill ia and ja */
    p = 0;
    ia[0] = p;
    for (i=0; i<nc; ++i)
    {
        for (j=faceptr[i]; j<faceptr[i+1]; ++j)
        {

            f  = cellfaces[j];
            boundaryface = (face2cell[2*f+0] == -1) ||
                           (face2cell[2*f+1] == -1);

            if ( boundaryface )
            {
                continue;
            }

            positive_sign = (i == face2cell[2*f]);
            theflux = positive_sign ? flux[f] : -flux[f];

            if ( theflux < 0)
            {
                ja[p++] = work[f];
            }
        }
        ia[i+1] = p;
    }
}


// ---------------------------------------------------------------------
static void
compute_reorder_sequence_graph(int           nc,
                               const int    *cellfaces,
                               const int    *facepos,
                               const int    *face2cell,
                               const double *flux,
                               int          *sequence,
                               int          *components,
                               int          *ncomponents,
                               int *ia, int *ja, int* work)
// ---------------------------------------------------------------------
{
    make_upwind_graph(nc, cellfaces, facepos, face2cell,
                      flux, ia, ja, work);

    tarjan (nc, ia, ja, sequence, components, ncomponents, work);

    assert (0 < *ncomponents);
    assert (*ncomponents <= nc);
}


// ---------------------------------------------------------------------
void
compute_sequence(const struct UnstructuredGrid* grid       ,
                 const double*                  flux       ,
                 int*                           sequence   ,
                 int*                           components ,
                 int*                           ncomponents)
// ---------------------------------------------------------------------
{
    const std::size_t nc = grid->number_of_cells;
    const std::size_t nf = grid->number_of_faces;
    const std::size_t sz = std::max(nf, 3 * nc);

    std::vector<int> work(sz);
    std::vector<int> ia  (nc + 1);
    std::vector<int> ja  (nf);  // A bit too much.

    compute_reorder_sequence_graph(grid->number_of_cells,
                                   grid->cell_faces,
                                   grid->cell_facepos,
                                   grid->face_cells,
                                   flux,
                                   sequence,
                                   components,
                                   ncomponents,
                                   & ia[0], & ja[0], & work[0]);
}


// ---------------------------------------------------------------------
void
compute_sequence_graph(const struct UnstructuredGrid* grid       ,
                       const double*                  flux       ,
                       int*                           sequence   ,
                       int*                           components ,
                       int*                           ncomponents,
                       int*                           ia         ,
                       int*                           ja         )
// ---------------------------------------------------------------------
{
    const std::size_t nc = grid->number_of_cells;
    const std::size_t nf = grid->number_of_faces;
    const std::size_t sz = std::max(nf, 3 * nc);

    std::vector<int> work(sz);

    compute_reorder_sequence_graph(grid->number_of_cells,
                                   grid->cell_faces,
                                   grid->cell_facepos,
                                   grid->face_cells,
                                   flux,
                                   sequence,
                                   components,
                                   ncomponents,
                                   ia, ja, & work[0]);
}


/* Local Variables:    */
/* c-basic-offset:4    */
/* End:                */
