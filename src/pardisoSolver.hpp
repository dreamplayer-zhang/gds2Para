#ifndef GDS2PARA_PARDISO_SOLVER_H_
#define GDS2PARA_PARDISO_SOLVER_H_

#include <iostream>
#include <fstream>
#include <string>

#include "fdtd.hpp"
#include "matrixTypeDef.hpp"

vector<myint> map_eIndexFromGrowzToGrowy(const myint Nx, const myint Ny, const myint Nz) {
    /* This function changes the global {e} index from layer growth along Z to that along Y.

    The index follows y - x - z ordering, and {e} is stacked layer by layer as
    {e_surface, e_volumn}.T at each layer. For the two cases here :
    - case 1 ~ grow along Z: {e} = {e_s, | e_v}.T = {ey,ex, | ez}.T, in which vector
            {ey} first runs through y for each x, then runs through x as by
            y - x - z ordering. Same for {ex} ~y->x and {ez} ~y->x.
    - case 2 ~ grow along Y: {e} = {ex,ez, | ey}.T at every layer.
            {ex}, {ez} and {ey} ~x->z, frist all x for each z then all z

    Input: num of bricks Nx*Ny*Nz
    Output: vector "eInd_map_z2y" of size N_e mapping global indices of {e}, eInd_map_z2y[oldInd] = newInd

    Yee's grid is used, with E at edge center and H at face center. Outmost
    boundaries are all E edges. Removal of {e} due to PEC BC has not been considered. */

    // Num of surface or volumetric unknown e at each layer
    myint n_surfEy_growZ        = Ny*(Nx + 1);
    myint n_surfEyEx            = n_surfEy_growZ + Nx*(Ny + 1);
    myint n_volEz               = (Nx + 1)*(Ny + 1);
    myint n_layerE_growZ        = n_surfEyEx + n_volEz;

    myint n_surfEx_growY        = Nx*(Nz + 1);
    myint n_surfExEz            = n_surfEx_growY + Nz*(Nx + 1);
    myint n_volEy               = (Nx + 1)*(Nz + 1);
    myint n_layerE_growY        = n_surfExEz + n_volEy;

    myint N_tot_E               = n_surfEyEx*(Nz + 1) + n_volEz*Nz;


    // y - x - z ordering
    // Grow along Z : {e} = { ey,ex, | ez }.T, y->x frist all y for each x then all x
    // Grow along Y : {e} = { ex,ez, | ey }.T, x->z frist all x for each z then all z

    vector<myint> eInd_map_z2y(N_tot_E, 0);

    // Map the edges along y direction, Ey
    for (myint iz = 0; iz < Nz + 1; iz++) {
        for (myint iy = 0; iy < Ny; iy++) {
            for (myint ix = 0; ix < Nx + 1; ix++) {
                eInd_map_z2y[iz*n_layerE_growZ + ix*(Ny)+iy] =
                    iy*n_layerE_growY + n_surfExEz + iz*(Nx + 1) + ix;
            }
        }
    }

    // Map the edges along x direction, Ex
    for (myint iz = 0; iz < Nz + 1; iz++) {
        for (myint iy = 0; iy < Ny + 1; iy++) {
            for (myint ix = 0; ix < Nx; ix++) {
                eInd_map_z2y[iz*n_layerE_growZ + n_surfEy_growZ + ix*(Ny + 1) + iy] =
                    iy*n_layerE_growY + iz*(Nx)+ix;
            }
        }
    }

    // Map the edges along z direction, Ez
    for (myint iz = 0; iz < Nz; iz++) {
        for (myint iy = 0; iy < Ny + 1; iy++) {
            for (myint ix = 0; ix < Nx + 1; ix++) {
                eInd_map_z2y[iz*n_layerE_growZ + n_surfEyEx + ix*(Ny + 1) + iy] =
                    iy*n_layerE_growY + n_surfEx_growY + iz*(Nx + 1) + ix;
            }
        }
    }

    return eInd_map_z2y;
}

// Reverse the map by swapping the index and value of the mapping array
vector<myint> reverseMap_eInd(const vector<myint> &eInd_map_z2y) {
    
    myint N_tot_E = eInd_map_z2y.size();
    vector<myint> eInd_map_y2z(N_tot_E, 0);

    for (myint id = 0; id < N_tot_E; id++) {
        eInd_map_y2z[eInd_map_z2y[id]] = id;
    }

    return eInd_map_y2z;
}

// Map specific Block_rowId and Block_colId to an unique Block index
inline myint mapRowColIdToBlockId(const myint B_rowId, const myint B_colId) {
    /* Explanation to notations here:

    Matrix S is partitioned into blocks as surface-vol-surface-... along row/col
    - Bolck ordering:     0s,0v,1s,1v,..., Nlayer_s
    - Block row/col Id:   0, 1, 2, 3, ..., 2*Nlayer
    - BlockId:            counted nnz block Id, row major. 

    Example: (Nlayer = 3, Nblock = 8*Nlayer + 1 = 25)
                    0s  0v  1s  1v  2s  2v  3s
        B_colId->   0   1   2   3   4   5   6
                                                    B_rowId (below) 
        BlockId:  | 0   1   2                  |    0   0s
                  | 3   4   5                  |    1   0v
                  | 6   7   8   9   10         |    2   1s
                  |         11  12  13         |    3   1v
                  |         14  15  16  17  18 |    4   2s
                  |                 19  20  21 |    5   2v
                  |                 22  23  24 |    6   3s

    Input: Block row/col Id
    Output: BlockId of this block    */
    
    myint BlockId = 0;
    myint layerId = B_rowId / 2;
    myint isVol = B_rowId % 2;

    if (layerId == 0) {     // B_rowId ~ 0s, 0v
        BlockId = B_rowId * 3 + B_colId;
    }
    else if(isVol == 1) {   // The B_row is n-v
        BlockId = 6 * layerId + B_colId + 3;
    }
    else {                  // The B_row is n-s
        BlockId = 6 * layerId + B_colId;
    }
    
    return BlockId;
}

// Compute y = alpha * A * x + beta * y and store computed dense matrix in y
int csrMultiplyDense(const sparse_matrix_t &csrA_mklHandle,     // mkl handle of csr A
    myint N_rows_x, complex<double> *xval,     // x,y ~ dense matrix stored in continuous memory array 
    denseFormatOfMatrix *y) {
    /* see doc: https://software.intel.com/en-us/onemkl-developer-reference-c-mkl-sparse-mm
    Example:
            y   = alpha * A   * x   + beta * y
            C11 = -1.0  * B12 * D0s + 1.0  * B11    */


    MKL_Complex16 alpha = { -1.0, 0.0 };
    MKL_Complex16 beta = { 1.0, 0.0 };
    struct matrix_descr descrA;             // Descriptor of main sparse matrix properties
    descrA.type = SPARSE_MATRIX_TYPE_GENERAL;

    vector<MKL_Complex16> yval_mklComplex(y->matrixSize);
    y->copyToMKL_Complex16(yval_mklComplex.data());
    myint x_matrixSize = N_rows_x * y->N_rows;
    vector<MKL_Complex16> xval_mklComplex(x_matrixSize);
    for (myint ind = 0; ind < x_matrixSize; ind++) {
        xval_mklComplex[ind].real = xval[ind].real();
        xval_mklComplex[ind].imag = xval[ind].imag();
    }

    // Compute y = alpha * A * x + beta * y
    mkl_sparse_z_mm(SPARSE_OPERATION_NON_TRANSPOSE,
        alpha,                              // alpha = -1.0
        csrA_mklHandle,                     // A
        descrA,
        SPARSE_LAYOUT_COLUMN_MAJOR,         // Describes the storage scheme for the dense matrix
        xval_mklComplex.data(),
        y->N_cols,
        N_rows_x,
        beta,                               // beta = 1.0
        yval_mklComplex.data(),             // Pointer to the memory array used by the vector to store its owned elements
        y->N_rows);

    return 0;
}

int eliminateVolumE(const vector<BlockType> &layerS, myint N_surfE, myint N_volE, denseFormatOfMatrix *preducedS) {
    /* This function eliminates e_vol from whole S matrix (all layers coupled) and obtain the
    2*2 block matrix related only to e_surf at single layer.

    Inputs:
        layerS: a vector containing 9 blocks of the whole matrix S, ~ within one layer
        N_surfE: number of {e}_surface at one surface, e.g. 0s
        N_volE: number of {e}_volumn at one layer, e.g. 0v
    Output:
        preducedS: pointing to memory of a vector storing 4 reduced dense blocks as {C11, C12, C21, C22}

    Each isolated layer here contains 2 surfaces and 1 middle volumn e, namely 0s-0v-1s. 
    A symbolic form is (each number represents the blockId in vector<BlockType>):
                
                 0s  0v  1s                                    0s  1s
    layerS =   | 0   1   2 |    0s      ==>     reducedS =   | 0'  1'|    0s
               | 3   4   5 |    0v                           | 2'  3'|    1s
               | 6   7   8 |    1s
    
    corresponding to block variable names in this function:
                 0s  0v  1s                                    0s  1s
    layerS ~   | B11 B12 B13 |  0s      ==>     reducedS ~   | C11 C12 |  0s
               | B21 B22 B23 |  0v                           | C21 C22 |  1s
               | B31 B32 B33 |  1s
    
    Due to the structure of matrix S, the central row of layerS is full, which could 
    be used to eliminate e0v as:  
        B21*e0s+B22*e0v+B23*e1s = 0  =>  e0v = -D0s*e0s - D1s*e1s, 
        where 
            D0s = inv(B22)*B21, D1s = inv(B22)*B23.
        Then the relation is: 
            C11 = B11 - B12*D0s,    C12 = B13 - B12*D1s
            C21 = B31 - B32*D0s,    C22 = B33 - B32*D1s
    */
    
    // Combine B21 and B23 in order to be sloved in one Pardiso run.
    BlockType vB21B23(layerS[3]);
    move(layerS[5].begin(), layerS[5].end(), back_inserter(vB21B23));
    denseFormatOfMatrix denseB21B23(N_volE, 2 * N_surfE);       // combined dense [B21, B23]
    denseB21B23.convertBlockTypeToDense(vB21B23);
    vB21B23.clear();

    // Solve D0s = inv(B22)*B21, D1s = inv(B22)*B23 in Pardiso
    csrFormatOfMatrix csrB22(N_volE, N_volE, layerS[4].size()); // CSR B22
    csrB22.convertBlockTypeToCsr(layerS[4]);
    denseFormatOfMatrix denseD0sD1s = 
        csrB22.backslashDense(denseB21B23);                     // combined dense [D0s, D1s]
    denseB21B23.~denseFormatOfMatrix();                         // free combined dense [B21, B23]

    // Convert B12, B32 to mkl internal CSR matrix handles
    sparse_matrix_t csrB12_mklHandle, csrB32_mklHandle;
    csrFormatOfMatrix csrB12(N_surfE, N_volE, layerS[1].size());
    csrB12.convertBlockTypeToCsr(layerS[1]);
    vector<MKL_Complex16> csrB12vals_mklCompl(csrB12.N_nnz);
    csrB12.copyToMKL_Complex16(csrB12vals_mklCompl.data());
    mkl_sparse_z_create_csr(&csrB12_mklHandle, SPARSE_INDEX_BASE_ZERO, csrB12.N_rows, csrB12.N_cols,
        csrB12.rows, csrB12.rows + 1, csrB12.cols, csrB12vals_mklCompl.data());
    csrFormatOfMatrix csrB32(N_surfE, N_volE, layerS[7].size());
    csrB32.convertBlockTypeToCsr(layerS[7]);
    vector<MKL_Complex16> csrB32vals_mklCompl(csrB32.N_nnz);
    csrB32.copyToMKL_Complex16(csrB32vals_mklCompl.data());
    mkl_sparse_z_create_csr(&csrB32_mklHandle, SPARSE_INDEX_BASE_ZERO, csrB32.N_rows, csrB32.N_cols,
        csrB32.rows, csrB32.rows + 1, csrB32.cols, csrB32vals_mklCompl.data());

    // Solve reducedS blocks C11 = B11 - B12*D0s, C12 = B13 - B12*D1s
    preducedS->convertBlockTypeToDense(layerS[0]);              // dense B11 & dense C11
    csrMultiplyDense(csrB12_mklHandle, denseD0sD1s.N_rows, denseD0sD1s.vals.data(), preducedS);
    myint matrixSizeD0s = N_volE * N_surfE;
    (preducedS + 1)->convertBlockTypeToDense(layerS[2]);        // dense B13 & dense C12
    csrMultiplyDense(csrB12_mklHandle, denseD0sD1s.N_rows, denseD0sD1s.vals.data() + matrixSizeD0s, preducedS + 1);
    mkl_sparse_destroy(csrB12_mklHandle);                       // free mkl CSR B12 handle

    // Solve reducedS blocks C21 = B31 - B32*D0s, C22 = B33 - B32*D1s
    (preducedS + 2)->convertBlockTypeToDense(layerS[6]);        // dense B31 & dense C21
    csrMultiplyDense(csrB32_mklHandle, denseD0sD1s.N_rows, denseD0sD1s.vals.data(), preducedS + 2);
    (preducedS + 3)->convertBlockTypeToDense(layerS[8]);        // dense B33 & dense C22
    csrMultiplyDense(csrB32_mklHandle, denseD0sD1s.N_rows, denseD0sD1s.vals.data() + matrixSizeD0s, preducedS + 3);
    mkl_sparse_destroy(csrB32_mklHandle);                       // free mkl CSR B32 handle

    denseD0sD1s.~denseFormatOfMatrix();                         // free combined dense [D0s, D1s]
    return 0;
}

// Reconstruct blocks stored in portportBlocks (or surfSurfBlocks) to S matrix in 1-D dense format
denseFormatOfMatrix reconstructBlocksToDense(const vector<vector<denseFormatOfMatrix>> &portportBlocks) {
    /* Example: N_layers = 2
            |D11     D12          |
            |D21   D22+D11'   D12'|     =   cascadedS
            |        D21'     D22'|
        Input: N_layers*4 2-D vector of blocks:  
            portportBlocks[0] = {D11, D12, D21, D22}
            portportBlocks[1] = {D11', D12', D21', D22'}
        Return:
            above block-tridiagonal square matrix cascadedS in 1-D dense format
    */

    // All blocks D and S are square matrix
    myint N_rows_perBlock = portportBlocks[0][0].N_rows;
    myint N_layers = portportBlocks.size();
    myint N_rows_S = N_rows_perBlock * (N_layers + 1);
    denseFormatOfMatrix cascadedS(N_rows_S, N_rows_S);
    denseFormatOfMatrix tempD(N_rows_perBlock, N_rows_perBlock);

    for (myint i_layer = 0; i_layer < N_layers; i_layer++) {
        for (myint j_col = 0; j_col < N_rows_perBlock; j_col++) {
            for (myint i_row = 0; i_row < N_rows_perBlock; i_row++) {
                myint ind_inBolck = j_col * N_rows_perBlock + i_row;

                // Upper triangular blocks Di i+1
                myint ind_inS_upperTri = (j_col + (i_layer+1) * N_rows_perBlock)* N_rows_S + i_row + i_layer * N_rows_perBlock;
                cascadedS.vals[ind_inS_upperTri] = portportBlocks[i_layer][1].vals[ind_inBolck];
                
                // Lower triangular blocks Di i-1
                myint ind_inS_lowerTri = (j_col + i_layer * N_rows_perBlock)* N_rows_S + i_row + (i_layer + 1) * N_rows_perBlock;
                cascadedS.vals[ind_inS_lowerTri] = portportBlocks[i_layer][2].vals[ind_inBolck];
            }
        }
    }

    // First diagonal block
    tempD = portportBlocks[0][0];
    for (myint j_col = 0; j_col < N_rows_perBlock; j_col++) {
        for (myint i_row = 0; i_row < N_rows_perBlock; i_row++) {
            myint ind_inBolck = j_col * N_rows_perBlock + i_row;
            myint ind_inS = j_col * N_rows_S + i_row;
            cascadedS.vals[ind_inS] = tempD.vals[ind_inBolck];
        }
    }

    // Last diagonal block
    tempD = portportBlocks[N_layers - 1][3];
    for (myint j_col = 0; j_col < N_rows_perBlock; j_col++) {
        for (myint i_row = 0; i_row < N_rows_perBlock; i_row++) {
            myint ind_inBolck = j_col * N_rows_perBlock + i_row;
            myint ind_inS = (j_col + N_layers * N_rows_perBlock)* N_rows_S + i_row + N_layers * N_rows_perBlock;
            cascadedS.vals[ind_inS] = tempD.vals[ind_inBolck];
        }
    }
    
    // The overlapped diagonal blocks (D22 and next D11')
    for (myint i_layer = 1; i_layer < N_layers; i_layer++) {
        tempD = portportBlocks[i_layer - 1][3].add(portportBlocks[i_layer][0]);
        for (myint j_col = 0; j_col < N_rows_perBlock; j_col++) {
            for (myint i_row = 0; i_row < N_rows_perBlock; i_row++) {
                myint ind_inBolck = j_col * N_rows_perBlock + i_row;
                myint ind_inS = (j_col + i_layer * N_rows_perBlock)* N_rows_S + i_row + i_layer * N_rows_perBlock;
                cascadedS.vals[ind_inS] = tempD.vals[ind_inBolck];
            }
        }
    }
    return cascadedS;
}

// Find the surface index of each port (layer growth along Y)
vector<myint> findSurfLocationOfPort(fdtdMesh *psys) {
/*  Method explained:
        - Each sourcePort has its position stored at psys->portCoor[sourcePort].y1, .y2, etc.
        - All node positions are stored at psys->yn, xn, zn.
        - Search psys->portCoor[sourcePort].y1 in psys->yn to find the surface index for that node.
    Notes: 
        - Many ports at 1 surf are counted once
        - For layer growth along y only and source current along x or z only

    Example: Nlayer = psys->N_cell_y = 3
        layer:      0s 0v 1s 1v 2s 2v 3s
        surfId:     0     1     2     3
        if port at: 0           2
        returned surfLocationOfPort = { 0, 2 }      */

    vector<myint> surfLocationOfPort;

    for (myint sourcePort = 0; sourcePort < psys->numPorts; sourcePort++) {
        double y_thisPort = psys->portCoor[sourcePort].y1[0];
        for (myint indy = 0; indy < psys->N_cell_y + 1; indy++) {   // search over all y nodes
            if (y_thisPort == psys->yn[indy]) {
                surfLocationOfPort.push_back(indy);
                break;
            }
        }
    }

    // Sort vector and erase duplicated surface index
    sort(surfLocationOfPort.begin(), surfLocationOfPort.end());
    surfLocationOfPort.erase( unique(surfLocationOfPort.begin(), surfLocationOfPort.end()), surfLocationOfPort.end() );

    return surfLocationOfPort;
}

denseFormatOfMatrix cascadeMatrixS(fdtdMesh *psys, double omegaHz) {
    /* This function cascades original S matrix in COO format to a dense matrix with only port surfaces left.
    Inputs:
        omegaHz (w): objective angular frequency in unit Hz
        matrix ShSe/mu:
                (psys->SRowId, psys->SColId, psys->Sval) ~ COO format
                psys->leng_S: number of nnz in ShSe/mu
    Return:
        cascadedS: matrix "(-w^2*D_eps+iw*D_sig+ShSe/mu)" with only {e}_portSurf left
    */

    // Num of e at each surface or each layer. Layer growth along y.
    myint Nx                    = psys->N_cell_x;
    myint Nz                    = psys->N_cell_z;
    myint n_surfExEz            = Nx*(Nz + 1) + Nz*(Nx + 1);
    myint n_volEy               = (Nx + 1)*(Nz + 1);
    myint n_layerE_growY        = n_surfExEz + n_volEy;
    myint N_layers              = psys->N_cell_y;   // num of layers

    // Store all Bolck matrices of the whole matrix S in a 2D vector
    vector< BlockType > Blocks(8 * N_layers + 1);

    // Determine the block id of each nnz element of S and store in corresponding block matrix
    myint nnzS_rowId, nnzS_colId, B_rowId, B_colId, BlockId;
    complex<double> cascadedS_val;
    vector<myint> eIndmap_z2y = map_eIndexFromGrowzToGrowy(psys->N_cell_x, psys->N_cell_y, psys->N_cell_z);
    vector<myint> eIndmap_y2z = reverseMap_eInd(eIndmap_z2y);   // the reverse map to z-growth global index in V0Vh code
    for (myint i_nnz = 0; i_nnz < psys->leng_S; i_nnz++) {
        nnzS_rowId = psys->SRowId[i_nnz];
        nnzS_colId = psys->SColId[i_nnz];
        cascadedS_val = psys->Sval[i_nnz];


        // For diagonal nnz, add "-w^2*eps+iw*sig" to psys->Sval (ShSe/mu)
        if (nnzS_rowId == nnzS_colId) {     // if at diagonal
            myint layerInd_alongZ = (eIndmap_y2z[nnzS_rowId] + psys->N_edge_v) / (psys->N_edge_s + psys->N_edge_v);
            double epsi_thisnnz = psys->stackEpsn[layerInd_alongZ] * EPSILON0;
            double sigma_thisnnz = 0;
            if (psys->markEdge[eIndmap_y2z[nnzS_rowId]] != 0) {
                sigma_thisnnz = SIGMA;
            }   // if inside a conductor 

            complex<double> epsi_sigma = { -omegaHz*omegaHz*epsi_thisnnz, omegaHz*sigma_thisnnz };
            cascadedS_val += epsi_sigma;    // -w^2*D_eps+iw*D_sig+ShSe/mu
        }


        B_rowId = nnzS_rowId / n_layerE_growY * 2 + (nnzS_rowId % n_layerE_growY) / n_surfExEz;
        B_colId = nnzS_colId / n_layerE_growY * 2 + (nnzS_colId % n_layerE_growY) / n_surfExEz;

        BlockId = mapRowColIdToBlockId(B_rowId, B_colId);

        // Shift the start row index and col index to be 0 inside each block
        nnzS_rowId = nnzS_rowId - (B_rowId / 2) * n_layerE_growY - (B_rowId % 2) * n_surfExEz;
        nnzS_colId = nnzS_colId - (B_colId / 2) * n_layerE_growY - (B_colId % 2) * n_surfExEz;

        Blocks[BlockId].push_back({ nnzS_rowId, nnzS_colId, cascadedS_val });
    }

    //// Free original matrix S to save memory
    //free(psys->SRowId);
    //free(psys->SColId);
    //free(psys->Sval);

    /******************** Start Cascading Matrix S from Blocks ***************************/
    
    // Consider PEC BCs on top or bottom (zmin & zmax) here
    // To be done!

    // Tell the size of sparse block matrices 
    myint N_surfE = n_surfExEz;
    myint N_volE = n_volEy;

    // Sort each block matrix by its row indices, 1st element of the tuple
    /* The purpose is to allow easy convert from COO to CSR format*/
    for (auto &block : Blocks) {
        sort(block.begin(), block.end(), ascendByRowInd);
    }

    // Half the value of overlapped ns-ns blocks between adjcent two layers
    /* The partitioned blocks no longer mean physical curl-curl opeartor, but mamatically, this is doable to
    cascade block matrix S. Any partition works like C = aC' + bC''. C = C' + C' makes codeing easier.*/
    for (myint nsns_BlockId = 8; nsns_BlockId < 8 * N_layers; nsns_BlockId += 8) {
        for (auto &nnz : Blocks[nsns_BlockId]) {
            nnz.val *= 0.5;
        }
    }

    // Eliminate e_volumn at each layer and store 4 reduced dense blocks of each layer
    vector<vector<denseFormatOfMatrix>> surfSurfBlocks(N_layers);   // N_layers*4 dense blocks
    for (myint i_layer = 0; i_layer < N_layers; i_layer++) {
        // Init and allocate memory for 4 dense blocks of each layer
        surfSurfBlocks[i_layer].reserve(4);
        for (myint i_block = 0; i_block < 4; i_block++) {           
            surfSurfBlocks[i_layer].push_back(denseFormatOfMatrix(N_surfE, N_surfE));
        }

        // From 9 blocks at this layer to 4, surfSurfBlocks[i_layer] = {C11, C12, C21, C22}
        vector<BlockType> layerS(Blocks.begin() + 8 * i_layer, Blocks.begin() + 8 * i_layer + 9);
        eliminateVolumE(layerS, N_surfE, N_volE, surfSurfBlocks[i_layer].data());
        layerS.clear();
    }
    Blocks.clear();     // free blocks of original whole matrix S to save memory

    // Cascade surf-surf blocks and only keep layers where ports are
    vector<myint> surfLocationOfPort = findSurfLocationOfPort(psys);
    
    /* Each layer: surfSurfBlocks[i_layer] = {C11, C12, C21, C22} */

    // Left -> first port: cascaded C22' = C22 - C21*inv(C11)*C12
    for (myint i_layer = 0; i_layer < surfLocationOfPort.front(); i_layer++) {
        surfSurfBlocks[i_layer][3] = surfSurfBlocks[i_layer][3].minus(
            surfSurfBlocks[i_layer][2].dot(surfSurfBlocks[i_layer][0].backslash(surfSurfBlocks[i_layer][1])));

        if (i_layer != N_layers) {  // if not reaching the right most layer
            // Add cascaded C22' at this layer to C11 at next layer
            surfSurfBlocks[i_layer + 1][0] = surfSurfBlocks[i_layer + 1][0].add(surfSurfBlocks[i_layer][3]);
        }
    }

    // Right -> last port: cascaded C11' = C11 - C12*inv(C22)*C21
    for (myint i_layer = N_layers-1; i_layer >= surfLocationOfPort.back(); i_layer--) {
        surfSurfBlocks[i_layer][0] = surfSurfBlocks[i_layer][0].minus(
            surfSurfBlocks[i_layer][1].dot(surfSurfBlocks[i_layer][3].backslash(surfSurfBlocks[i_layer][2])));

        if (i_layer != 0) {         // if not reaching the left most layer
            // Add cascaded C11' at this layer to C22 at previous layer
            surfSurfBlocks[i_layer - 1][3] = surfSurfBlocks[i_layer - 1][3].add(surfSurfBlocks[i_layer][0]);
        }
    }

    // Middle: 
    /* For the middle, cascaded the middle surface ({e}_midLayer) between 2 layers
         For example:
            |C11     C12          | {e}_thisPortLayer
            |C21   C22+C11'   C12'| {e}_midLayer       , where  4 Cij at thisPortLayer
            |        C21'     C22'| {e}_midLayer+1              4 Cij' at the midLayer
            To cascade the middle surface {e}_midLayer, the precedures are:
                1) tempC22 = C22+C11'
                2) tempD21 = tempC22\C21, tempD12 = tempC22\C12'
                3) cascaded 4 blocks still store in Cij at thisPortLayer:
                    cascaded C11 = C11 - C12*tempD21,  cascaded C12 = 0 - C12*tempD12
                    cascaded C21 =  0 - C21'*tempD21,  cascaded C22 = C22' - C21'*tempD12
      
      Then, the cascaded 4 blocks for each port (index 0 <= i_port < N_ports-1, except last port) will be stored in corresponding layer.
            let: thisPortLayer = surfLocationOfPort[i_port];
                 nextPortLayer = surfLocationOfPort[i_port + 1];
            i_port's 4 blocks are stored in:
                 surfSurfBlocks[thisPortLayer] = {D11, D12, D21, D22}
            next port's 4 blocks are stored in:
                 surfSurfBlocks[nextPortLayer] = {D11', D12', D21', D22'}
            To reconstruct cascaded S matrix, we need to organize above cascaded blocks as
                |D11     D12          | {e}_thisPort
                |D21   D22+D11'   D12'| {e}_nextPort
                |        D21'     D22'| {e}_nextnextPort
    */
    denseFormatOfMatrix tempC22(N_surfE, N_surfE);              // tempC22 = C22+C11'
    denseFormatOfMatrix tempD21(N_surfE, N_surfE);              // tempD21 = tempC22\C21
    denseFormatOfMatrix tempD12(N_surfE, N_surfE);              // tempD12 = tempC22\C12'
    for (myint i_port = 0; i_port < surfLocationOfPort.size() - 1; i_port++) {                  // all the intervals between ports
        myint thisPortLayer = surfLocationOfPort[i_port];       // surface index and layer index of this port (i_port)
        myint nextPortLayer = surfLocationOfPort[i_port + 1];   // surface index and layer index of next port
        for (myint i_midLayer = thisPortLayer + 1; i_midLayer < nextPortLayer; i_midLayer++) {    // all middle layers between 2 ports
            tempC22 = surfSurfBlocks[thisPortLayer][3].add(surfSurfBlocks[i_midLayer][0]);
            tempD21 = tempC22.backslash(surfSurfBlocks[thisPortLayer][2]);
            tempD12 = tempC22.backslash(surfSurfBlocks[i_midLayer][1]);
            surfSurfBlocks[thisPortLayer][0] = surfSurfBlocks[thisPortLayer][0].minus(
                surfSurfBlocks[thisPortLayer][1].dot(tempD21));
            surfSurfBlocks[thisPortLayer][1] = surfSurfBlocks[thisPortLayer][1].dot(tempD12).multiplyScalar(-1.0);
            surfSurfBlocks[thisPortLayer][2] = surfSurfBlocks[i_midLayer][2].dot(tempD21).multiplyScalar(-1.0);
            surfSurfBlocks[thisPortLayer][3] = surfSurfBlocks[i_midLayer][3].minus(
                surfSurfBlocks[i_midLayer][2].dot(tempD12));
        }
    }
    
    // Collect all cascaded blocks only related to port surfaces
    myint N_ports = surfLocationOfPort.size();
    if (N_ports == 1) {     // when only one port, return the block C11
        myint thisPortLayer = surfLocationOfPort[0];
        return surfSurfBlocks[thisPortLayer][0];
    }
    vector<vector<denseFormatOfMatrix>> portportBlocks(N_ports-1);   // (N_ports-1)*4 dense blocks
    for (myint i_port = 0; i_port < N_ports - 1; i_port++) {
        myint thisPortLayer = surfLocationOfPort[i_port];
        portportBlocks[i_port] = surfSurfBlocks[thisPortLayer];
    }
    surfSurfBlocks.clear(); // free surf-surf blocks 

    return reconstructBlocksToDense(portportBlocks);
}

// Calculate all the simulated freq points and store in a vector
vector<double> calAllFreqPointsHz(const fdtdMesh &sys) {
    vector<double> vFreqHz;

    vFreqHz.push_back(sys.freqStart * sys.freqUnit);          // First frequency in sweep

    for (int id = 1; id < sys.nfreq; id++) {                  // When nfreq > 1
        if (sys.freqScale == 1) {
            vFreqHz.push_back((sys.freqStart + id * (sys.freqEnd - sys.freqStart) / (sys.nfreq - 1)) * sys.freqUnit);
        }   // Linear interpolation of frequency sweep
        else {
            vFreqHz.push_back(sys.freqStart * sys.freqUnit * pow(sys.freqEnd / sys.freqStart, (id * 1.0 / (sys.nfreq - 1))));
        }   // Logarithmic interpolation of frequency sweep
    }

    return vFreqHz;
}

// Assign source current density (-iw{j}) for every port excitation
denseFormatOfMatrix assignRhsJForAllPorts(fdtdMesh *psys, double omegaHz) {
    /* -iw{j} for each port is stored continusly in denseFormatOfMatrix (col by col)
    
    - step 1: get psys->portCoor[sourcePort].portEdge[sourcePortSide][], the stored the global edge index 
              (grow along Z, no PEC removal) of all excitation current lines when excited at port index "sourcePort"
    - step 2: map to (grow along Y, no PEC removal)
    - step 3: cascade to port surfaces    */

    // Map -iw{j} at at edges from original index (grow Z) to new index (grow Y) 
    myint N_allEdges = psys->N_edge;                                // num of all edges in the computational domain
    denseFormatOfMatrix RhsJ_SI(N_allEdges, psys->numPorts);        // -iw{j} at all edges
    vector<myint> eIndmap_z2y = map_eIndexFromGrowzToGrowy(psys->N_cell_x, psys->N_cell_y, psys->N_cell_z);
    for (myint sourcePort = 0; sourcePort < psys->numPorts; sourcePort++) {
        for (myint sourcePortSide = 0; sourcePortSide < psys->portCoor[sourcePort].multiplicity; sourcePortSide++) {
            for (myint inde = 0; inde < psys->portCoor[sourcePort].portEdge[sourcePortSide].size(); inde++) {
                myint ind_j_growZ = psys->portCoor[sourcePort].portEdge[sourcePortSide][inde];  // j edge index (growZ, no PEC removal)
                myint ind_j_growY = eIndmap_z2y[ind_j_growZ];                                   // map to j edge index (growY, no PEC removal)
                ind_j_growY += sourcePort * N_allEdges;                                         // index shifted by port number
                RhsJ_SI.vals[ind_j_growY] = { 0.0, -1.0 * omegaHz * (psys->portCoor[sourcePort].portDirection[sourcePortSide]) };
            }
        }
    }

    // Only keep -iw{j} to port surfaces (cascading)
    myint Nx = psys->N_cell_x;
    myint Nz = psys->N_cell_z;
    myint N_surfExEz = Nx*(Nz + 1) + Nz*(Nx + 1);                               // number of edges at one surface
    myint n_volEy = (Nx + 1)*(Nz + 1);                                          // number of volumn edges in one layer
    myint N_allCascadedEdges = psys->numPorts * N_surfExEz;                     // number of all edges in kept surfaces
    denseFormatOfMatrix cascadedRhsJ_SI(N_allCascadedEdges, psys->numPorts);    // -iw{j} only in port surfaces
    vector<myint> surfLocationOfPort = findSurfLocationOfPort(psys);
    for (myint excitedPort = 0; excitedPort < psys->numPorts; excitedPort++) {
        for (myint ind_thisPort = 0; ind_thisPort < psys->numPorts; ind_thisPort++) {           // for each kept port surface
            for (myint j_ind = 0; j_ind < N_surfExEz; j_ind++) {    // for all e at this surface, index starting from 0
                // Cascaded index = indJ at this surface + shift by previous port surfaces + shift by previous port excitation
                myint indJ_cascaded = j_ind + ind_thisPort * N_surfExEz + excitedPort * N_allCascadedEdges;

                // For original noncascaded index
                myint surfInd_thisPort = surfLocationOfPort[ind_thisPort];
                myint shift_surfvol = surfInd_thisPort * (N_surfExEz + n_volEy);    // index shift from all surf-vol {e} before this surface
                myint indJ_noncasc = j_ind + shift_surfvol + excitedPort * N_allEdges;

                cascadedRhsJ_SI.vals[indJ_cascaded] = RhsJ_SI.vals[indJ_noncasc];
            }   
        }   
    }

    return cascadedRhsJ_SI;
}

// Reconstruct {e} as in V0Vh part (grow Z, removed PEC) from cascaded {e} solution (grow Y, no PEC removal)
denseFormatOfMatrix reconstruct_e(fdtdMesh *psys, const denseFormatOfMatrix &cascadedeField_SI, double excitedPort) {
    /*  Inputs:
            - cascadedeField_SI: of size N_allCascadedEdges * psys->numPorts
            - excitedPort: excited port index
        Return:
            - eField_oneExcit: of size (psys->N_edge - psys->bden) * 1
    */

    myint Nx = psys->N_cell_x;
    myint Nz = psys->N_cell_z;
    myint N_surfExEz = Nx*(Nz + 1) + Nz*(Nx + 1);                               // number of edges at one surface
    myint n_volEy = (Nx + 1)*(Nz + 1);                                          // number of volumn edges in one layer
    myint N_allCascadedEdges = psys->numPorts * N_surfExEz;                     // number of all edges in kept surfaces
    myint N_allEdges = psys->N_edge;                                            // num of all edges in the computational domain
    myint N_allEdgesNoPEC = psys->N_edge - psys->bden;                          // num of all edges after removing PEC
    vector<myint> surfLocationOfPort = findSurfLocationOfPort(psys);            // surf index of the port's location

    
    vector<myint> eIndmap_z2y = map_eIndexFromGrowzToGrowy(psys->N_cell_x, psys->N_cell_y, psys->N_cell_z);
    vector<myint> eIndmap_y2z = reverseMap_eInd(eIndmap_z2y);                   // map (grow Y, no PEC removal) to (grow Z, no PEC removal)
    myint* &eMap_GrowZ_pec2nopec = psys->mapEdge;

    // Reconstruct {e} solution when excited at port index "excitedPort"
    denseFormatOfMatrix eField_oneExcit(N_allEdgesNoPEC, 1);
    for (myint ind_thisPort = 0; ind_thisPort < psys->numPorts; ind_thisPort++) {           // for each kept port surface
        for (myint e_ind = 0; e_ind < N_surfExEz; e_ind++) {    // for all e at this surface, index starting from 0
            // Cascaded index = e_ind at this surface + shift by previous port surfaces + shift by previous port excitation
            myint eInd_cascaded = e_ind + ind_thisPort * N_surfExEz + excitedPort * N_allCascadedEdges;

            // For original noncascaded index
            myint surfInd_thisPort = surfLocationOfPort[ind_thisPort];
            myint shift_surfvol = surfInd_thisPort * (N_surfExEz + n_volEy);    // index shift from all surf-vol {e} before this surface
            myint eInd_growYnoPEC = e_ind + shift_surfvol;                      // Step 1: original index at (grow Y, no PEC removal)
            myint eInd_growZnoPEC = eIndmap_y2z[eInd_growYnoPEC];               // Step 2: map (grow Y, no PEC removal) to (grow Z, no PEC removal)
            myint eInd_growZpec = psys->mapEdge[eInd_growZnoPEC];               // Step 3: map (grow Z, no PEC removal) to (grow Z, removed PEC)
            
            if (eInd_growZpec != -1) {
                eField_oneExcit.vals[eInd_growZpec] = cascadedeField_SI.vals[eInd_cascaded];
            }   // psys->mapEdge maps PEC edge index to -1, meaning not in domain
        }
    }

    return eField_oneExcit;
}

// Solve E field and Z-parameters in Pardiso, solve layer by layer. (under developing)
void solveE_Zpara_layered(fdtdMesh *psys) {

    // Initilize Z-parameters for all frequencies
    psys->x.assign(psys->numPorts * psys->numPorts * psys->nfreq, complex<double>(0., 0.));

    vector<double> vFreqHz = calAllFreqPointsHz(*psys);
    for (int indFreq = 0; indFreq < vFreqHz.size(); indFreq++) {    // for each computed freq point
        double omegaHz = 2.0 * M_PI * vFreqHz[indFreq];

        // Cascaded system matrix (-w^2*D_eps+iw*D_sig+ShSe/mu)
        denseFormatOfMatrix cascadedS = cascadeMatrixS(psys, omegaHz);

        // Cascaded -iwJ and {e}. All excitations at each port are solved together
        denseFormatOfMatrix cascadedRhsJ_SI = assignRhsJForAllPorts(psys, omegaHz);     // -iw{j} in unit (A * m^-2 / s)
        denseFormatOfMatrix cascadedeField_SI = cascadedS.backslash(cascadedRhsJ_SI);   // {e} in unit (V/m)

        // For each port excitation, reconstruct {e} and solve Z-parameters
        for (myint excitedPort = 0; excitedPort < psys->numPorts; excitedPort++) {
            denseFormatOfMatrix eField_oneExcit = reconstruct_e(psys, cascadedeField_SI, excitedPort);
            psys->Construct_Z_V0_Vh(eField_oneExcit.vals.data(), indFreq, excitedPort);
        }
    }

    // Print Z-parameters
    psys->print_z_V0_Vh();

}

// Solve E field and Z-parameters in Pardiso, solve the whole structure as reference
void solveE_Zpara_reference(fdtdMesh *psys) {

    // All computed freq points
    vector<double> vFreqHz = calAllFreqPointsHz(*psys);

    // Initilize Z-parameters for all frequencies
    psys->x.assign(psys->numPorts * psys->numPorts * psys->nfreq, complex<double>(0., 0.));

    // At each frequency, solve all ports together
    for (int indFreq = 0; indFreq < vFreqHz.size(); indFreq++) {
        reference(psys, indFreq, psys->SRowId, psys->SColId, psys->Sval);
    }   

    // Print Z-parameters
    psys->print_z_V0_Vh();

}

#endif