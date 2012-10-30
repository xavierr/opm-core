/*
  Copyright 2012 SINTEF ICT, Applied Mathematics.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <opm/core/transport/reorder/TransportModelTracerTof.hpp>
#include <opm/core/grid.h>
#include <opm/core/utility/ErrorMacros.hpp>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace Opm
{


    /// Construct solver.
    /// \param[in] grid      A 2d or 3d grid.
    TransportModelTracerTof::TransportModelTracerTof(const UnstructuredGrid& grid,
                                                     const bool use_multidim_upwind)
        : grid_(grid), use_multidim_upwind_(use_multidim_upwind)
    {
    }




    /// Solve for time-of-flight.
    /// \param[in]  darcyflux         Array of signed face fluxes.
    /// \param[in]  porevolume        Array of pore volumes.
    /// \param[in]  source            Source term. Sign convention is:
    ///                                 (+) inflow flux,
    ///                                 (-) outflow flux.
    /// \param[out] tof               Array of time-of-flight values.
    void TransportModelTracerTof::solveTof(const double* darcyflux,
                                           const double* porevolume,
                                           const double* source,
                                           std::vector<double>& tof)
    {
        darcyflux_ = darcyflux;
        porevolume_ = porevolume;
        source_ = source;
#ifndef NDEBUG
        // Sanity check for sources.
        const double cum_src = std::accumulate(source, source + grid_.number_of_cells, 0.0);
        if (std::fabs(cum_src) > *std::max_element(source, source + grid_.number_of_cells)*1e-2) {
            THROW("Sources do not sum to zero: " << cum_src);
        }
#endif
        tof.resize(grid_.number_of_cells);
        std::fill(tof.begin(), tof.end(), 0.0);
        tof_ = &tof[0];
        if (use_multidim_upwind_) {
            face_tof_.resize(grid_.number_of_faces);
            std::fill(face_tof_.begin(), face_tof_.end(), 0.0);
        }
        reorderAndTransport(grid_, darcyflux);
    }




    void TransportModelTracerTof::solveSingleCell(const int cell)
    {
        // Compute flux terms.
        // Sources have zero tof, and therefore do not contribute
        // to upwind_term. Sinks on the other hand, must be added
        // to the downwind_flux (note sign change resulting from
        // different sign conventions: pos. source is injection,
        // pos. flux is outflow).
        double upwind_term = 0.0;
        double downwind_flux = std::max(-source_[cell], 0.0);
        for (int i = grid_.cell_facepos[cell]; i < grid_.cell_facepos[cell+1]; ++i) {
            int f = grid_.cell_faces[i];
            double flux;
            int other;
            // Compute cell flux
            if (cell == grid_.face_cells[2*f]) {
                flux  = darcyflux_[f];
                other = grid_.face_cells[2*f+1];
            } else {
                flux  =-darcyflux_[f];
                other = grid_.face_cells[2*f];
            }
            // Add flux to upwind_term or downwind_flux, if interior.
            if (other != -1) {
                if (flux < 0.0) {
                    if (use_multidim_upwind_) {
                        const double ftof = multidimUpwindTof(f, other);
                        face_tof_[f] = ftof;
                        upwind_term += flux*ftof;
                    } else {
                        upwind_term += flux*tof_[other];
                    }
                } else {
                    downwind_flux += flux;
                }
            }
        }

        // Compute tof.
        tof_[cell] = (porevolume_[cell] - upwind_term)/downwind_flux;
    }




    void TransportModelTracerTof::solveMultiCell(const int num_cells, const int* cells)
    {
        std::cout << "Pretending to solve multi-cell dependent equation with " << num_cells << " cells." << std::endl;
        for (int i = 0; i < num_cells; ++i) {
            solveSingleCell(cells[i]);
        }
    }


    double TransportModelTracerTof::multidimUpwindTof(const int face, const int upwind_cell) const
    {
        // Implements multidim upwind according to
        // "Multidimensional upstream weighting for multiphase transport on general grids"
        // by Keilegavlen, Kozdon, Mallison.
        // However, that article does not give a 3d extension other than noting that using
        // multidimensional upwinding in the XY-plane and not in the Z-direction may be
        // a good idea. We have here attempted some generalization, by looking at all
        // face-neighbours across edges as upwind candidates, and giving them all uniform weight.
        // This will over-weight the immediate upstream cell value in an extruded 2d grid with
        // one layer (top and bottom no-flow faces will enter the computation) compared to the
        // original 2d case. Improvements are welcome.

        // Identify the adjacent faces of the upwind cell.
        const int* face_nodes_beg = grid_.face_nodes + grid_.face_nodepos[face];
        const int* face_nodes_end = grid_.face_nodes + grid_.face_nodepos[face + 1];
        ASSERT(face_nodes_end - face_nodes_beg == 2 || grid_.dimensions != 2);
        adj_faces_.clear();
        for (int hf = grid_.cell_facepos[upwind_cell]; hf < grid_.cell_facepos[upwind_cell + 1]; ++hf) {
            const int f = grid_.cell_faces[hf];
            if (f != face) {
                const int* f_nodes_beg = grid_.face_nodes + grid_.face_nodepos[f];
                const int* f_nodes_end = grid_.face_nodes + grid_.face_nodepos[f + 1];
                // Find out how many vertices they have in common.
                // Using simple linear searches since sets are small.
                int num_common = 0;
                for (const int* f_iter = f_nodes_beg; f_iter < f_nodes_end; ++f_iter) {
                    num_common += std::count(face_nodes_beg, face_nodes_end, *f_iter);
                }
                if (num_common == grid_.dimensions - 1) {
                    // Neighbours over an edge (3d) or vertex (2d).
                    adj_faces_.push_back(f);
                } else {
                    ASSERT(num_common == 0);
                }
            }
        }

        // Indentify adjacent faces with inflows, compute omega_star, omega,
        // add up contributions.
        const int num_adj = adj_faces_.size();
        ASSERT(num_adj == face_nodes_end - face_nodes_beg);
        const double flux_face = std::fabs(darcyflux_[face]);
        double tof = 0.0;
        for (int ii = 0; ii < num_adj; ++ii) {
            const int f = adj_faces_[ii];
            const double influx_f = (grid_.face_cells[2*f] == upwind_cell) ? -darcyflux_[f] : darcyflux_[f];
            const double omega_star = influx_f/flux_face;
            // SPU
            // const double omega = 0.0;
            // TMU
            // const double omega = omega_star > 0.0 ? std::min(omega_star, 1.0) :  0.0;
            // SMU
            const double omega = omega_star > 0.0 ? omega_star/(1.0 + omega_star) : 0.0;
            tof += (1.0 - omega)*tof_[upwind_cell] + omega*face_tof_[f];
        }

        // For now taking a simple average.
        return tof/double(num_adj);
    }

} // namespace Opm
